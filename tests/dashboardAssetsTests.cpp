#include "dashboardAssetTable.h"

#include <gtest/gtest.h>
#include <picosha2.h>
#include <zlib.h>

#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace
{

std::string gunzip(const unsigned char *data, size_t size)
{
    z_stream stream{};
    // 16+MAX_WBITS: a gzip header, not a zlib one
    if(inflateInit2(&stream, 16+MAX_WBITS)!=Z_OK)
        return std::string();

    std::string out;
    char buffer[65536];
    stream.next_in=const_cast<unsigned char *>(data);
    stream.avail_in=static_cast<uInt>(size);
    int result=Z_OK;
    while(result==Z_OK)
    {
        stream.next_out=reinterpret_cast<unsigned char *>(buffer);
        stream.avail_out=sizeof(buffer);
        result=inflate(&stream, Z_NO_FLUSH);
        out.append(buffer, sizeof(buffer)-stream.avail_out);
    }
    inflateEnd(&stream);
    return result==Z_STREAM_END?out:std::string();
}

} // namespace

#ifdef ARBITERAI_DASHBOARD_ASSET_DIR

TEST(DashboardAssets, EmbedsTheFourFilesWithTheirMimeTypes)
{
    size_t count=0;
    arbiterAI::server::dashboardAssets(count);
    EXPECT_EQ(count, 4u);

    std::map<std::string, std::string> expected={
        {"shell.html", "text/html; charset=utf-8"},
        {"app.js", "application/javascript"},
        {"app.wasm", "application/wasm"},
        {"app.data", "application/octet-stream"}
    };
    for(const auto &[name, mime]:expected)
    {
        const arbiterAI::server::DashboardAsset *asset=arbiterAI::server::findDashboardAsset(name.c_str());
        ASSERT_NE(asset, nullptr)<<name;
        EXPECT_EQ(std::string(asset->mimeType), mime)<<name;
    }
}

TEST(DashboardAssets, RawBytesAreTheBuiltFiles)
{
    for(const char *name:{"shell.html", "app.js", "app.wasm", "app.data"})
    {
        const arbiterAI::server::DashboardAsset *asset=arbiterAI::server::findDashboardAsset(name);
        ASSERT_NE(asset, nullptr)<<name;

        std::ifstream file(std::string(ARBITERAI_DASHBOARD_ASSET_DIR)+"/"+name, std::ios::binary);
        ASSERT_TRUE(file.good())<<name;
        std::string onDisk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        ASSERT_EQ(asset->size, onDisk.size())<<name;
        EXPECT_TRUE(std::string(reinterpret_cast<const char *>(asset->data), asset->size)==onDisk)<<name;
    }
}

TEST(DashboardAssets, GzipCopyInflatesToTheRawBytes)
{
    for(const char *name:{"shell.html", "app.js", "app.wasm", "app.data"})
    {
        const arbiterAI::server::DashboardAsset *asset=arbiterAI::server::findDashboardAsset(name);
        ASSERT_NE(asset, nullptr)<<name;

        EXPECT_LT(asset->gzipSize, asset->size)<<name;
        std::string inflated=gunzip(asset->gzipData, asset->gzipSize);
        ASSERT_EQ(inflated.size(), asset->size)<<name;
        EXPECT_TRUE(inflated==std::string(reinterpret_cast<const char *>(asset->data), asset->size))<<name;
    }
}

TEST(DashboardAssets, EtagIsTheQuotedSha256OfTheRawBytes)
{
    for(const char *name:{"shell.html", "app.js", "app.wasm", "app.data"})
    {
        const arbiterAI::server::DashboardAsset *asset=arbiterAI::server::findDashboardAsset(name);
        ASSERT_NE(asset, nullptr)<<name;

        std::string hash=picosha2::hash256_hex_string(asset->data, asset->data+asset->size);
        EXPECT_EQ(std::string(asset->etag), "\""+hash.substr(0, 32)+"\"")<<name;
    }
}

#else

TEST(DashboardAssets, NoneWithoutTheWebDashboard)
{
    size_t count=1;
    EXPECT_EQ(arbiterAI::server::dashboardAssets(count), nullptr);
    EXPECT_EQ(count, 0u);
}

#endif
