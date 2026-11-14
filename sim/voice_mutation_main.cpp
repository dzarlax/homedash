#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *,size_t);
int main(int argc,char **argv) {
    if(argc!=2) return 2; std::ifstream in(argv[1],std::ios::binary);
    std::vector<uint8_t> seed((std::istreambuf_iterator<char>(in)),{}); if(seed.empty()) return 2;
    std::mt19937_64 rng(42); auto end=std::chrono::steady_clock::now()+std::chrono::seconds(300); uint64_t count=0;
    while(std::chrono::steady_clock::now()<end) {
        auto data=seed; unsigned changes=1+rng()%12;
        while(changes--) { auto op=rng()%3; if(op==0&&!data.empty()) data[rng()%data.size()]=(uint8_t)rng(); else if(op==1&&data.size()<65536) data.insert(data.begin()+rng()%(data.size()+1),(uint8_t)rng()); else if(!data.empty()) data.erase(data.begin()+rng()%data.size()); }
        LLVMFuzzerTestOneInput(data.data(),data.size()); ++count;
    }
    std::printf("mutation checks passed: %llu\n",(unsigned long long)count);
}
