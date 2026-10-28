#include "scenarios/slime_mold/SlimeMoldScene.h"
#include "scenarios/slime_mold/SlimeMoldModel.h"
#include "core/Window.h"
#include "core/Context.h"
#include "core/ComputeDevice.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <glm/geometric.hpp>

using namespace emergences::scenarios::slime_mold;
using emergences::sim::Backend;
namespace {
void require(bool pass,const char* message) { if(!pass) throw std::runtime_error(message); }
float positionError=0, trailError=0;
void compare(const SlimeMoldState& a, const SlimeMoldState& b, float tolerance=0.0003F) {
    require(a.agents.size()==b.agents.size() && a.trail.size()==b.trail.size(),"GPU state dimensions mismatch");
    for(std::size_t i=0;i<a.agents.size();++i) {
        for(glm::length_t axis=0;axis<3;++axis) {
            const float error=std::abs(a.agents[i].position[axis]-b.agents[i].position[axis]);
            positionError=std::max(positionError,error);
            require(std::isfinite(error) && error<tolerance,"CPU/GPU position mismatch");
            require(std::abs(a.agents[i].velocity[axis]-b.agents[i].velocity[axis])<tolerance*10,"CPU/GPU velocity mismatch");
        }
        require(a.agents[i].id==b.agents[i].id && a.agents[i].color==b.agents[i].color,"GPU changed IDs/colors");
    }
    for(std::size_t i=0;i<a.trail.size();++i) {
        float error=std::abs(a.trail[i]-b.trail[i]); trailError=std::max(trailError,error);
        require(std::isfinite(b.trail[i]) && error<tolerance,"CPU/GPU trail mismatch");
    }
}
}
int main() {
    try {
        emergences::core::Window window{320,240,"Slime mold compute tests"};
        emergences::core::Context context{window};
#ifndef NDEBUG
        require(context.validationEnabled(),"Debug GPU tests require Vulkan validation");
#endif
        {
            emergences::core::VulkanComputeDevice device{context};
            const auto path=(std::filesystem::path{EMERGENCES_SHADER_DIRECTORY}/"compute/slime_mold.comp.spv").string();
            SlimeMoldScene cpu{257,1337}, gpu{257,1337};
            gpu.initializeGpu(device,path); gpu.setBackend(Backend::GPU);
            for (const std::size_t count : {0U,1U,257U,4096U}) {
                cpu.setCount(count); gpu.setCount(count);
                cpu.advance(1.0/60); gpu.advance(1.0/60); compare(cpu.readback(),gpu.readback());
            }
            // Exact mass/stencil checks include wrap seams and zero agents.
            cpu.setCount(0); gpu.setCount(0);
            auto state=cpu.readback(); state.trail[0]=9;
            auto p=cpu.parameters(); p.diffusion=60; p.evaporation=0;
            cpu.setParameters(p); gpu.setParameters(p); cpu.setState(state); gpu.setState(state);
            for(int tick=0;tick<20;++tick) { cpu.advance(1.0/60); gpu.advance(1.0/60); }
            compare(cpu.readback(),gpu.readback());
            const auto spread=gpu.readback();
            require(std::abs(std::accumulate(spread.trail.begin(),spread.trail.end(),0.0)-9)<1e-4,"GPU diffusion lost mass");

            // 100k agents in one cell: integer atomic deposition has no lost
            // updates, fixed cell capacity, or float atomic extension requirement.
            cpu.setCount(SlimeMoldScene::MaxAgents); gpu.setCount(SlimeMoldScene::MaxAgents);
            p.speed=0; p.wander=0; p.turnSpeed=0; p.diffusion=0; p.depositRate=0.3F;
            cpu.setParameters(p); gpu.setParameters(p);
            state=cpu.readback();
            for(auto& a:state.agents) { a.position={0,0,0}; a.reserved=0; }
            cpu.setState(state); gpu.setState(state);
            cpu.advance(1.0/60); gpu.advance(1.0/60); compare(cpu.readback(),gpu.readback());
            require(std::abs(gpu.readback().trail[model::cell(0,0)]-
                static_cast<float>(SlimeMoldScene::MaxAgents)*model::deposit(p)/model::DepositScale)<1e-4F,
                "GPU contention lost deposits");
            p.depositRate=100; cpu.setParameters(p); gpu.setParameters(p);
            cpu.advance(1.0/60); gpu.advance(1.0/60); compare(cpu.readback(),gpu.readback());
            require(gpu.readback().trail[model::cell(0,0)]==SlimeMoldScene::MaxTrail,"GPU concentration cap failed");

            // Controlled sensor steering, periodic movement, and multi-step PRNG.
            cpu.setCount(1); gpu.setCount(1); cpu.setFoodCount(0); gpu.setFoodCount(0);
            p.speed=30; p.turnSpeed=12; p.wander=0; p.depositRate=0; p.evaporation=0.35F; p.diffusion=18;
            cpu.setParameters(p); gpu.setParameters(p);
            state=cpu.readback(); state.agents[0].position={49.9F,0,0}; state.agents[0].reserved=0;
            state.trail[model::cell(49.9F+std::cos(p.sensorAngle)*p.sensorDistance,std::sin(p.sensorAngle)*p.sensorDistance)]=10;
            cpu.setState(state); gpu.setState(state);
            cpu.advance(1.0/60); gpu.advance(1.0/60); compare(cpu.readback(),gpu.readback());
            require(gpu.readback().agents[0].position.x < -49 && gpu.readback().agents[0].reserved>0,"GPU sensor/wrap failed");
            cpu.reset(); gpu.reset(); p.wander=1; p.turnSpeed=0;
            cpu.setParameters(p); gpu.setParameters(p);
            for(int tick=0;tick<120;++tick) { cpu.advance(1.0/60); gpu.advance(1.0/60); }
            compare(cpu.readback(),gpu.readback(),0.002F);
            const auto snapshot=gpu.readback(); const auto steps=gpu.totalSteps(); gpu.setPaused(true);
            gpu.setBackend(Backend::CPU); compare(snapshot,gpu.readback());
            gpu.setBackend(Backend::GPU); compare(snapshot,gpu.readback());
            require(gpu.totalSteps()==steps && gpu.paused(),"GPU switch lost clock or pause");
            const auto cpuDisplay=cpu.refreshDisplay(0.4F,true);
            require(gpu.refreshDisplay(0.4F,true).empty(),"GPU display read back implicitly");
            std::vector<emergences::sim::Agent> display(gpu.renderCount(true));
            device.download(gpu.renderBuffer(),std::as_writable_bytes(std::span{display}));
            require(display.size()==cpuDisplay.size(),"GPU display count mismatch");
            for(std::size_t i=0;i<SlimeMoldScene::CellCount;++i)
                require(display[i].position==cpuDisplay[i].position && display[i].size==cpuDisplay[i].size &&
                    glm::length(display[i].color-cpuDisplay[i].color)<1e-5F,"GPU field display mismatch");
            compare(snapshot,gpu.readback());
        }
        context.waitIdle();
        require(context.validationErrorCount()==0,"Vulkan validation failed");
        std::cout<<"Slime mold GPU checks passed; max position error "<<positionError<<", field error "<<trailError<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
