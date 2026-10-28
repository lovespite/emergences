#include "scenarios/slime_mold/SlimeMoldScene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace emergences::sim;
using namespace emergences::scenarios::slime_mold;
namespace {
void require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool threw = false; try { f(); } catch (const std::exception&) { threw = true; }
    require(threw, "Expected injected failure");
}
// Contract/ownership test double, not a substitute for executing the real shader.
class MemoryDevice final : public ComputeDevice {
public:
    std::map<BufferHandle,std::vector<std::byte>> buffers;
    std::set<PipelineHandle> pipelines;
    std::uint64_t next = 1;
    int downloads = 0, uploadsUntilFailure = -1;
    bool failDownload = false, failExecute = false;
    std::vector<std::uint32_t> stages;
    BufferHandle createBuffer(std::size_t size) override { auto h=next++; buffers[h].resize(size); return h; }
    void destroyBuffer(BufferHandle h) noexcept override { buffers.erase(h); }
    void upload(BufferHandle h, std::span<const std::byte> bytes) override {
        if (uploadsUntilFailure == 0) throw std::runtime_error("Injected upload failure");
        if (uploadsUntilFailure > 0) --uploadsUntilFailure;
        require(bytes.size() <= buffers.at(h).size(), "Upload overflow");
        std::copy(bytes.begin(),bytes.end(),buffers.at(h).begin());
    }
    void download(BufferHandle h, std::span<std::byte> bytes) override {
        if (failDownload) throw std::runtime_error("Injected download failure");
        ++downloads; require(bytes.size() <= buffers.at(h).size(), "Download overflow");
        std::copy_n(buffers.at(h).begin(),bytes.size(),bytes.begin());
    }
    PipelineHandle createPipeline(std::string_view, std::uint32_t bindings, std::uint32_t constants) override {
        require(bindings==7 && constants==80,"GPU ABI mismatch"); auto h=next++; pipelines.insert(h); return h;
    }
    void destroyPipeline(PipelineHandle h) noexcept override { pipelines.erase(h); }
    void execute(std::span<const ComputeDispatch> work) override {
        stages.clear();
        for (const auto& w : work) {
            require(pipelines.contains(w.pipeline) && w.groupsX>0 && w.constants.size()==80 && w.buffers.size()==7,
                "Invalid dispatch");
            require(std::set<BufferHandle>(w.buffers.begin(),w.buffers.end()).size()==7,"Aliased GPU buffers");
            for(auto h:w.buffers) require(buffers.contains(h),"Dead GPU buffer");
            std::array<std::uint32_t,4> counts; std::memcpy(counts.data(),w.constants.data(),sizeof(counts));
            stages.push_back(counts[2]);
            if(counts[2]==1) buffers.at(w.buffers[1])=buffers.at(w.buffers[0]);
            if(counts[2]==2) buffers.at(w.buffers[3])=buffers.at(w.buffers[2]);
            if(failExecute && counts[2]==2) throw std::runtime_error("Injected partial dispatch failure");
        }
    }
    void waitIdle() override {}
};
void same(const SlimeMoldState& a,const SlimeMoldState& b) {
    require(a.trail==b.trail && a.agents.size()==b.agents.size(),"Transfer lost field/count");
    require(std::memcmp(a.agents.data(),b.agents.data(),a.agents.size()*sizeof(Agent))==0,"Transfer lost agents");
}
}
int main() {
    try {
        MemoryDevice device;
        {
            SlimeMoldScene scene{257}; scene.advance(1.0/60); scene.setPaused(true);
            auto before=scene.readback(); auto steps=scene.totalSteps();
            scene.initializeGpu(device,"fake"); scene.setBackend(Backend::GPU);
            require(scene.agents().empty() && scene.trail().empty() && scene.renderBuffer()!=0,"GPU leaked stale CPU views");
            same(before,scene.readback());
            const auto downloaded=device.downloads;
            scene.singleStep(); scene.refreshDisplay(0.4F,true);
            require(device.downloads==downloaded,"GPU step/display performed readback");
            require(scene.totalSteps()==steps+1 && scene.paused(),"Transfer lost clock/pause");
            scene.setBackend(Backend::CPU); same(before,scene.readback());
            scene.setBackend(Backend::GPU);
            const auto buffer=scene.renderBuffer(); const auto liveBuffers=device.buffers.size();
            device.uploadsUntilFailure=1;
            rejects([&]{scene.setCount(100);});
            device.uploadsUntilFailure=-1;
            require(device.buffers.size()==liveBuffers && scene.renderBuffer()==buffer && scene.count()==257,
                "Failed reset destroyed live resources");
            same(before,scene.readback());
            device.failDownload=true; rejects([&]{scene.setBackend(Backend::CPU);}); device.failDownload=false;
            require(scene.backend()==Backend::GPU,"Failed download committed switch");
            steps=scene.totalSteps(); device.failExecute=true; rejects([&]{scene.singleStep();}); device.failExecute=false;
            same(before,scene.readback()); require(scene.totalSteps()==steps,"Failed execution advanced clock");
            scene.singleStep(); require(device.stages==std::vector<std::uint32_t>{0,1,2},"Compute phases out of order");
            scene.setCount(0); scene.singleStep();
            require(device.stages==std::vector<std::uint32_t>{0,2},"Empty population must still evolve field");
            scene.setBackend(Backend::CPU);
            device.uploadsUntilFailure=0; rejects([&]{scene.initializeGpu(device,"fake");}); device.uploadsUntilFailure=-1;
            require(device.pipelines.size()==1 && device.buffers.size()==liveBuffers,"Failed initialization leaked resources");
        }
        require(device.buffers.empty() && device.pipelines.empty(),"GPU ownership leak");
        std::cout << "Slime mold backend contracts passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
