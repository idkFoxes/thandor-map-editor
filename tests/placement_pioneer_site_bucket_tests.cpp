#include "thandor/pioneer_site_bucket.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

int main() {
    using thandor::pioneer_site_bucket_count;
    auto require=[](bool condition,const char* why){if(!condition)throw std::runtime_error(why);};
    const auto fail=pioneer_site_bucket_count(false,777U,false,0x2000U);
    require(!fail.query_succeeded,"both placement modes failed but query accepted");
    const auto mode0=pioneer_site_bucket_count(false,777U,true,0x2000U);
    require(mode0.query_succeeded&&mode0.bucket_count==0U,"mode-0 fallback changed");
    const auto zero=pioneer_site_bucket_count(true,0U,false,0x2000U);
    require(zero.query_succeeded&&zero.bucket_count==0U,"zero placement count changed");
    const auto both=pioneer_site_bucket_count(true,0x4001U,true,0x2000U);
    require(both.query_succeeded&&both.bucket_count==0U,"mode-0 success must zero buckets");
    for (std::uint32_t count: {1U,0x1FFFU,0x2000U}) {
        const auto result=pioneer_site_bucket_count(true,count,false,0x2000U);
        require(result.query_succeeded&&result.bucket_count==1U,"single quantum boundary");
    }
    require(pioneer_site_bucket_count(true,0x2001U,false,0x2000U).bucket_count==2U,
            "round-up quantum boundary");
    require(pioneer_site_bucket_count(true,0x8000U,false,0x2000U).bucket_count==4U,
            "four-bucket chain threshold");
    require(pioneer_site_bucket_count(true,0x8001U,false,0x2000U).bucket_count==5U,
            "five-bucket no-chain threshold");
    require(pioneer_site_bucket_count(true,UINT32_MAX,false,1U).bucket_count==UINT32_MAX,
            "overflow in uint32 bucket round-up");
    require(!pioneer_site_bucket_count(true,1U,false,0U).query_succeeded,
            "malformed zero quantum must not divide");
    std::cout<<"V590_PIONEER_SITE_BUCKET PASS\n";
}
