#include "arbiterAI/providers/stableDiffusion.h"

#include <stable-diffusion.h>
#include <zlib.h>

#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cstring>

namespace arbiterAI
{

namespace
{

/// Standard base64 alphabet encoder.
std::string base64Encode(const uint8_t *data, size_t len)
{
    static const char *tbl="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len+2)/3)*4);
    size_t i=0;
    for(; i+3<=len; i+=3)
    {
        uint32_t n=(data[i]<<16)|(data[i+1]<<8)|data[i+2];
        out.push_back(tbl[(n>>18)&0x3F]);
        out.push_back(tbl[(n>>12)&0x3F]);
        out.push_back(tbl[(n>>6)&0x3F]);
        out.push_back(tbl[n&0x3F]);
    }
    if(i<len)
    {
        uint32_t n=data[i]<<16;
        bool two=(i+1<len);
        if(two) n|=data[i+1]<<8;
        out.push_back(tbl[(n>>18)&0x3F]);
        out.push_back(tbl[(n>>12)&0x3F]);
        out.push_back(two?tbl[(n>>6)&0x3F]:'=');
        out.push_back('=');
    }
    return out;
}

void appendBE32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>((x>>24)&0xFF));
    v.push_back(static_cast<uint8_t>((x>>16)&0xFF));
    v.push_back(static_cast<uint8_t>((x>>8)&0xFF));
    v.push_back(static_cast<uint8_t>(x&0xFF));
}

/// Append a PNG chunk (length, type, data, CRC-32 over type+data).
void appendChunk(std::vector<uint8_t> &png, const char *type, const std::vector<uint8_t> &data)
{
    appendBE32(png, static_cast<uint32_t>(data.size()));
    size_t crcStart=png.size();
    png.insert(png.end(), type, type+4);
    png.insert(png.end(), data.begin(), data.end());
    uLong crc=crc32(0L, Z_NULL, 0);
    crc=crc32(crc, png.data()+crcStart, static_cast<uInt>(png.size()-crcStart));
    appendBE32(png, static_cast<uint32_t>(crc));
}

/// Encode raw 8-bit RGB(A) pixels as a PNG (deflate via zlib). Returns empty on failure.
std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, uint32_t channels, const uint8_t *pixels)
{
    if(channels!=3 && channels!=4)
        return {};

    // Filter each scanline with filter type 0 (None): [0][row bytes].
    const size_t rowBytes=static_cast<size_t>(width)*channels;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes+1)*height);
    for(uint32_t y=0; y<height; ++y)
    {
        raw.push_back(0);
        raw.insert(raw.end(), pixels+y*rowBytes, pixels+(y+1)*rowBytes);
    }

    uLongf compBound=compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> comp(compBound);
    if(compress2(comp.data(), &compBound, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED)!=Z_OK)
        return {};
    comp.resize(compBound);

    std::vector<uint8_t> png={0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    appendBE32(ihdr, width);
    appendBE32(ihdr, height);
    ihdr.push_back(8);                        // bit depth
    ihdr.push_back(channels==4?6:2);          // color type: 2=RGB, 6=RGBA
    ihdr.push_back(0);                        // compression
    ihdr.push_back(0);                        // filter
    ihdr.push_back(0);                        // interlace
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", comp);
    appendChunk(png, "IEND", {});

    return png;
}

} // namespace

StableDiffusion::StableDiffusion()
    : BaseProvider("stable-diffusion")
{
}

StableDiffusion::~StableDiffusion()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for(auto &entry:m_contexts)
    {
        if(entry.second)
            free_sd_ctx(entry.second);
    }
    m_contexts.clear();
}

ErrorCode StableDiffusion::completion(const CompletionRequest &request,
    const ModelInfo &model,
    CompletionResponse &response)
{
    return ErrorCode::NotImplemented;
}

ErrorCode StableDiffusion::streamingCompletion(const CompletionRequest &request,
    std::function<void(const std::string &)> callback)
{
    return ErrorCode::NotImplemented;
}

ErrorCode StableDiffusion::getEmbeddings(const EmbeddingRequest &request,
    EmbeddingResponse &response)
{
    return ErrorCode::NotImplemented;
}

std::string StableDiffusion::resolveModelPath(const ModelInfo &model)
{
    return resolveDownloadableModelFile(model);
}

sd_ctx_t *StableDiffusion::acquireContext(const std::string &modelPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it=m_contexts.find(modelPath);
    if(it!=m_contexts.end())
        return it->second;

    sd_ctx_params_t params;
    sd_ctx_params_init(&params);
    params.model_path=modelPath.c_str();

    sd_ctx_t *ctx=new_sd_ctx(&params);
    if(!ctx)
    {
        spdlog::warn("StableDiffusion provider: failed to load model from '{}'", modelPath);
        return nullptr;
    }

    m_contexts.emplace(modelPath, ctx);
    return ctx;
}

ErrorCode StableDiffusion::generateImage(const ImageGenerationRequest &request,
    const ModelInfo &model,
    ImageGenerationResponse &response)
{
    std::string modelPath=resolveModelPath(model);
    if(modelPath.empty())
    {
        spdlog::warn("StableDiffusion provider: no model file configured for '{}'", model.model);
        return ErrorCode::ModelNotFound;
    }

    // Defaults from the model's sd_options; request fields override.
    bool haveOpts=model.sdOptions.is_object();
    int width=haveOpts ? model.sdOptions.value("width", 512) : 512;
    int height=haveOpts ? model.sdOptions.value("height", 512) : 512;
    int defaultSteps=haveOpts ? model.sdOptions.value("steps", 0) : 0;

    // Parse "WIDTHxHEIGHT" if provided (OpenAI-style "size") — overrides the default.
    if(request.size.has_value())
    {
        int w=0, h=0;
        if(std::sscanf(request.size->c_str(), "%dx%d", &w, &h)==2 && w>0 && h>0)
        {
            width=w;
            height=h;
        }
    }

    sd_ctx_t *ctx=acquireContext(modelPath);
    if(!ctx)
        return ErrorCode::ModelLoadError;

    sd_img_gen_params_t gp;
    sd_img_gen_params_init(&gp);
    gp.prompt=request.prompt.c_str();
    std::string negative=request.negativePrompt.value_or("");
    gp.negative_prompt=negative.c_str();
    gp.width=width;
    gp.height=height;
    gp.batch_count=request.n.value_or(1);
    gp.seed=request.seed.value_or(-1);
    if(request.steps.has_value() && request.steps.value()>0)
        gp.sample_params.sample_steps=request.steps.value();
    else if(defaultSteps>0)
        gp.sample_params.sample_steps=defaultSteps;
    if(haveOpts && model.sdOptions.contains("cfg_scale"))
        gp.sample_params.guidance.txt_cfg=model.sdOptions.value("cfg_scale", gp.sample_params.guidance.txt_cfg);

    sd_image_t *images=nullptr;
    int numImages=0;

    {
        // generate_image is not reentrant on a shared context — serialize.
        std::lock_guard<std::mutex> lock(m_inferenceMutex);
        bool ok=generate_image(ctx, &gp, &images, &numImages);
        if(!ok || !images || numImages<=0)
        {
            spdlog::warn("StableDiffusion provider: generate_image failed for '{}'", model.model);
            if(images)
                free(images);
            return ErrorCode::GenerationError;
        }
    }

    for(int i=0; i<numImages; ++i)
    {
        const sd_image_t &img=images[i];
        std::vector<uint8_t> png=encodePng(img.width, img.height, img.channel, img.data);
        if(!png.empty())
        {
            GeneratedImage out;
            out.b64Json=base64Encode(png.data(), png.size());
            out.revisedPrompt=request.prompt;
            response.images.push_back(std::move(out));
        }
        // sd allocates each image's pixel buffer with malloc.
        if(img.data)
            free(img.data);
    }
    free(images);

    if(response.images.empty())
        return ErrorCode::GenerationError;

    response.model=model.model;
    response.provider="stable-diffusion";

    return ErrorCode::Success;
}

} // namespace arbiterAI
