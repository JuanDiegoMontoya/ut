#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <stack>
#include <functional>
#include <iostream>
#include <string_view>
#include <optional>
#include <fstream>
#include <cassert>

void VK_CHECK(VkResult err)
{
  if (err)
  {
    std::cout << "Vulkan error: " << err << '\n';
    std::abort();
  }
}

VkInstance gInstance;
VkDebugUtilsMessengerEXT gDebugMessenger;
VkDevice gDevice;
VkPhysicalDevice gPhysicalDevice;
VkQueue gQueue;
uint32_t gQueueFamilyIndex;
VmaAllocator gAllocator;
std::stack<std::function<void()>> gDeletionStack;
VkPipeline gPipeline;

void init()
{
  vkb::InstanceBuilder builder;
  auto builder_ret = builder.set_app_name("Vulkan")
    .request_validation_layers(true)
    .require_api_version(1, 3, 0)
    .use_default_debug_messenger()
    .set_headless(true)
    .build();

  vkb::Instance vkb_instance = builder_ret.value();
  gInstance = vkb_instance.instance;
  gDebugMessenger = vkb_instance.debug_messenger;

  vkb::PhysicalDeviceSelector selector{ vkb_instance };

  vkb::PhysicalDevice physicalDevice = selector
    .set_minimum_version(1, 3)
    .select()
    .value();

  vkb::DeviceBuilder deviceBuilder{ physicalDevice };
  vkb::Device vkbDevice = deviceBuilder
    .build()
    .value();

  gDevice = vkbDevice.device;
  gPhysicalDevice = physicalDevice.physical_device;

  gQueue = vkbDevice.get_queue(vkb::QueueType::compute).value();
  gQueueFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::compute).value();

  VmaAllocatorCreateInfo allocatorInfo
  {
    .physicalDevice = gPhysicalDevice,
    .device = gDevice,
    .instance = gInstance,
  };
  vmaCreateAllocator(&allocatorInfo, &gAllocator);
}

void deinit()
{
  while (!gDeletionStack.empty())
  {
    auto& fn = gDeletionStack.top();
    fn();
    gDeletionStack.pop();
  }
  vmaDestroyAllocator(gAllocator);
}

std::optional<VkShaderModule> LoadShaderModule(std::string_view filePath)
{
  std::ifstream file(filePath.data(), std::ios::ate | std::ios::binary);

  if (!file.is_open())
  {
    return std::nullopt;
  }

  size_t filesize = static_cast<size_t>(file.tellg());
  assert(filesize % sizeof(uint32_t) == 0);
  std::vector<uint32_t> buffer(filesize / sizeof(uint32_t));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(buffer.data()), filesize);

  VkShaderModuleCreateInfo shaderModuleCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = filesize,
    .pCode = buffer.data()
  };

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(gDevice, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
  {
    return std::nullopt;
  }

  return shaderModule;
}

int main()
{
  init();

  auto shaderModule = LoadShaderModule("shaders/test.comp.spv").value_or(VkShaderModule{});
  gDeletionStack.push([=] { vkDestroyShaderModule(gDevice, shaderModule, nullptr); });

  auto shaderStageInfo = VkPipelineShaderStageCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = shaderModule,
    .pName = "main"
  };
  auto descriptorSetLayoutBinding = VkDescriptorSetLayoutBinding
  {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
  };
  auto descriptorSetLayoutInfo = VkDescriptorSetLayoutCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 1,
    .pBindings = &descriptorSetLayoutBinding
  };
  auto descriptorSetLayout = VkDescriptorSetLayout{};
  VK_CHECK(vkCreateDescriptorSetLayout(gDevice, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout));
  gDeletionStack.push([=] { vkDestroyDescriptorSetLayout(gDevice, descriptorSetLayout, nullptr); });

  auto pipelineLayoutInfo = VkPipelineLayoutCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &descriptorSetLayout,
  };
  auto pipelineLayout = VkPipelineLayout{};
  VK_CHECK(vkCreatePipelineLayout(gDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout));
  gDeletionStack.push([=] { vkDestroyPipelineLayout(gDevice, pipelineLayout, nullptr); });

  auto pipelineInfo = VkComputePipelineCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = shaderStageInfo,
    .layout = pipelineLayout,
  };
  VK_CHECK(vkCreateComputePipelines(gDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gPipeline));
  gDeletionStack.push([=] { vkDestroyPipeline(gDevice, gPipeline, nullptr); });

  deinit();
  return 0;
}