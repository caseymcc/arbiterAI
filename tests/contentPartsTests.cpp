#include <gtest/gtest.h>

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/base64.h"

namespace arbiterAI
{

namespace
{

/// 1x1 PNG, base64-encoded.
const char *kTinyPngBase64=
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

std::string dataUri(const std::string &mime, const std::string &b64)
{
    return "data:"+mime+";base64,"+b64;
}

} // namespace

TEST(Base64Test, DecodesKnownVectors)
{
    std::vector<uint8_t> out;

    ASSERT_TRUE(base64Decode("", out));
    EXPECT_TRUE(out.empty());

    ASSERT_TRUE(base64Decode("Zg==", out));
    EXPECT_EQ(std::string(out.begin(), out.end()), "f");

    ASSERT_TRUE(base64Decode("Zm8=", out));
    EXPECT_EQ(std::string(out.begin(), out.end()), "fo");

    ASSERT_TRUE(base64Decode("Zm9vYmFy", out));
    EXPECT_EQ(std::string(out.begin(), out.end()), "foobar");
}

TEST(Base64Test, IgnoresWhitespaceAndRejectsGarbage)
{
    std::vector<uint8_t> out;

    ASSERT_TRUE(base64Decode("Zm9v\nYmFy\n", out));
    EXPECT_EQ(std::string(out.begin(), out.end()), "foobar");

    EXPECT_FALSE(base64Decode("Zm9v*mFy", out));
    EXPECT_FALSE(base64Decode("Zg===AA", out));
}

TEST(Base64Test, DecodesPngMagicBytes)
{
    std::vector<uint8_t> out;
    ASSERT_TRUE(base64Decode(kTinyPngBase64, out));
    ASSERT_GE(out.size(), 8u);

    EXPECT_EQ(out[0], 0x89);
    EXPECT_EQ(out[1], 'P');
    EXPECT_EQ(out[2], 'N');
    EXPECT_EQ(out[3], 'G');
}

TEST(ContentPartsTest, PlainStringContentHasNoParts)
{
    std::vector<ContentPart> parts;
    std::string error;

    ASSERT_TRUE(contentToParts(nlohmann::json("just text"), parts, error))<<error;
    EXPECT_TRUE(parts.empty());
}

TEST(ContentPartsTest, ParsesTextAndImageParts)
{
    nlohmann::json content=nlohmann::json::array({
        {{"type", "text"}, {"text", "What is in this image?"}},
        {{"type", "image_url"}, {"image_url", {{"url", dataUri("image/png", kTinyPngBase64)},
                                               {"detail", "auto"}}}}
    });

    std::vector<ContentPart> parts;
    std::string error;
    ASSERT_TRUE(contentToParts(content, parts, error))<<error;

    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].type, "text");
    EXPECT_EQ(parts[0].text, "What is in this image?");
    EXPECT_EQ(parts[1].type, "image");
    EXPECT_EQ(parts[1].mimeType, "image/png");
    EXPECT_FALSE(parts[1].imageData.empty());

    // The flattened text view must still contain only the text parts.
    EXPECT_EQ(contentToString(content), "What is in this image?");
}

TEST(ContentPartsTest, AcceptsResponsesApiInputImageSpelling)
{
    nlohmann::json content=nlohmann::json::array({
        {{"type", "input_image"}, {"image_url", dataUri("image/jpeg", kTinyPngBase64)}}
    });

    std::vector<ContentPart> parts;
    std::string error;
    ASSERT_TRUE(contentToParts(content, parts, error))<<error;

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].type, "image");
    EXPECT_EQ(parts[0].mimeType, "image/jpeg");
}

TEST(ContentPartsTest, RejectsRemoteAndMalformedImages)
{
    std::vector<ContentPart> parts;
    std::string error;

    nlohmann::json remote=nlohmann::json::array({
        {{"type", "image_url"}, {"image_url", {{"url", "https://example.com/cat.png"}}}}
    });
    EXPECT_FALSE(contentToParts(remote, parts, error));
    EXPECT_NE(error.find("data:"), std::string::npos);

    nlohmann::json noComma=nlohmann::json::array({
        {{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64"}}}}
    });
    EXPECT_FALSE(contentToParts(noComma, parts, error));

    nlohmann::json notBase64=nlohmann::json::array({
        {{"type", "image_url"}, {"image_url", {{"url", "data:image/png,rawbytes"}}}}
    });
    EXPECT_FALSE(contentToParts(notBase64, parts, error));

    nlohmann::json badPayload=nlohmann::json::array({
        {{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64,!!!!"}}}}
    });
    EXPECT_FALSE(contentToParts(badPayload, parts, error));
}

TEST(ContentPartsTest, MessageImageDetection)
{
    Message textOnly;
    textOnly.role="user";
    textOnly.content="hello";

    Message withImage;
    withImage.role="user";
    withImage.content="describe";
    ContentPart image;
    image.type="image";
    image.mimeType="image/png";
    image.imageData={0x89, 'P', 'N', 'G'};
    withImage.parts.push_back(image);

    EXPECT_FALSE(textOnly.hasImageParts());
    EXPECT_TRUE(withImage.hasImageParts());
    EXPECT_FALSE(hasImageContent({textOnly}));
    EXPECT_TRUE(hasImageContent({textOnly, withImage}));
}

} // namespace arbiterAI
