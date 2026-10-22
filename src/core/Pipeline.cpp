#include "core/Pipeline.h"

#include "core/Context.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace emergences::core {

namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open SPIR-V shader: " + path.string());
    }

    const std::streamoff length = file.tellg();
    // A SPIR-V module begins with a five-word header. Reading into words also
    // guarantees the alignment required by VkShaderModuleCreateInfo::pCode.
    if (length < 5 * static_cast<std::streamoff>(sizeof(std::uint32_t)) ||
        length % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        throw std::runtime_error("Invalid SPIR-V shader size: " + path.string());
    }
    const auto byteCount = static_cast<std::uintmax_t>(length);
    if (byteCount > std::numeric_limits<std::size_t>::max() ||
        byteCount > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("SPIR-V shader is too large: " + path.string());
    }

    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byteCount));
    if (!file) {
        throw std::runtime_error("Cannot read SPIR-V shader: " + path.string());
    }
    if (words.front() != 0x07230203U) {
        throw std::runtime_error("Invalid SPIR-V shader magic: " + path.string());
    }
    return words;
}

class ShaderModule final {
public:
    ShaderModule(VkDevice device, const std::filesystem::path& path) : device_(device) {
        const auto words = readSpirv(path);
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = words.size() * sizeof(std::uint32_t);
        info.pCode = words.data();
        check(vkCreateShaderModule(device_, &info, nullptr, &module_), "Creating a shader module");
    }

    ~ShaderModule() { vkDestroyShaderModule(device_, module_, nullptr); }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    [[nodiscard]] VkShaderModule handle() const noexcept { return module_; }

private:
    VkDevice device_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

} // namespace

Pipeline::Pipeline(const Context& context, VkRenderPass renderPass,
                   const std::filesystem::path& shaderDirectory)
    : Pipeline(context, renderPass, PipelineOptions{
        .vertexShader = shaderDirectory / "smoke" / "triangle.vert.spv",
        .fragmentShader = shaderDirectory / "smoke" / "triangle.frag.spv"}) {}

Pipeline::Pipeline(const Context& context, VkRenderPass renderPass, const PipelineOptions& options)
    : device_(context.device()) {
    try {
        const ShaderModule vertex{device_, options.vertexShader};
        const ShaderModule fragment{device_, options.fragmentShader};

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex.handle();
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment.handle();
        stages[1].pName = "main";

        // The smoke shader derives its three vertices from gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(options.bindings.size());
        vertexInput.pVertexBindingDescriptions = options.bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(options.attributes.size());
        vertexInput.pVertexAttributeDescriptions = options.attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = options.topology;

        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = options.depthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = options.depthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending{};
        blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blending.attachmentCount = 1;
        blending.pAttachments = &blendAttachment;

        constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (options.descriptorLayout != VK_NULL_HANDLE) {
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &options.descriptorLayout;
        }
        const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0, options.pushConstantBytes};
        if (options.pushConstantBytes != 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
        }
        check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_), "Creating the pipeline layout");

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = layout_;
        info.renderPass = renderPass;
        info.subpass = 0;
        info.basePipelineIndex = -1;
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_),
              "Creating the graphics pipeline");
    } catch (...) {
        cleanup();
        throw;
    }
}

Pipeline::~Pipeline() { cleanup(); }

void Pipeline::cleanup() noexcept {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

} // namespace emergences::core
