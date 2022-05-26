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
#include <cstdint>

struct BufferAllocation
{
  VkBuffer buffer;
  VmaAllocation allocation;
};

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
VkPipelineLayout gPipelineLayout;
VkDescriptorPool gDescriptorPool;
VkDescriptorSetLayout gDescriptorSetLayout;
VkDescriptorSet gDescriptorSet;
VkCommandPool gCommandPool;
VkCommandBuffer gCommandBuffer;
VkFence gFence;
VkQueryPool gQueryPool;
float gTimestampPeriod;

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

void InitVulkan()
{
  vkb::InstanceBuilder builder;
  auto vkbInstance = builder.set_app_name("Vulkan")
    .request_validation_layers(true)
    .require_api_version(1, 2, 0)
    .use_default_debug_messenger()
    .set_headless(true)
    .build()
    .value();

  gInstance = vkbInstance.instance;
  gDebugMessenger = vkbInstance.debug_messenger;
  
  gDeletionStack.push([=] { vkDestroyInstance(gInstance, nullptr); });
  gDeletionStack.push([=] { vkb::destroy_debug_utils_messenger(gInstance, gDebugMessenger); });

  vkb::PhysicalDeviceSelector selector{ vkbInstance };

  auto requiredFeatures = VkPhysicalDeviceVulkan12Features
  {
    .hostQueryReset = true
  };

  vkb::PhysicalDevice vkbPhysicalDevice = selector
    .set_minimum_version(1, 2)
    .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
    .set_required_features_12(requiredFeatures)
    .select()
    .value();

  if (vkbPhysicalDevice.properties.limits.timestampComputeAndGraphics == false)
  {
    std::cout << "Device does not support compute timestamps" << '\n';
    std::abort();
  }

  gTimestampPeriod = vkbPhysicalDevice.properties.limits.timestampPeriod;

  vkb::DeviceBuilder deviceBuilder{ vkbPhysicalDevice };
  vkb::Device vkbDevice = deviceBuilder
    .build()
    .value();
  gDeletionStack.push([=] { vkb::destroy_device(vkbDevice); });

  gDevice = vkbDevice.device;
  gPhysicalDevice = vkbPhysicalDevice.physical_device;

  gQueue = vkbDevice.get_queue(vkb::QueueType::compute).value();
  gQueueFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::compute).value();

  VmaAllocatorCreateInfo allocatorInfo
  {
    .physicalDevice = gPhysicalDevice,
    .device = gDevice,
    .instance = gInstance,
  };
  vmaCreateAllocator(&allocatorInfo, &gAllocator);
  gDeletionStack.push([=] { vmaDestroyAllocator(gAllocator); });
}

void InitDescriptors()
{
  auto poolSize = VkDescriptorPoolSize{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 10 };
  auto descriptorPoolInfo = VkDescriptorPoolCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 1,
    .pPoolSizes = &poolSize
  };
  VK_CHECK(vkCreateDescriptorPool(gDevice, &descriptorPoolInfo, nullptr, &gDescriptorPool));
  gDeletionStack.push([=] {vkDestroyDescriptorPool(gDevice, gDescriptorPool, nullptr); });

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
  VK_CHECK(vkCreateDescriptorSetLayout(gDevice, &descriptorSetLayoutInfo, nullptr, &gDescriptorSetLayout));
  gDeletionStack.push([=] { vkDestroyDescriptorSetLayout(gDevice, gDescriptorSetLayout, nullptr); });

  auto descriptorSetAllocInfo = VkDescriptorSetAllocateInfo
  {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = gDescriptorPool,
    .descriptorSetCount = 1,
    .pSetLayouts = &gDescriptorSetLayout
  };
  VK_CHECK(vkAllocateDescriptorSets(gDevice, &descriptorSetAllocInfo, &gDescriptorSet));
  //gDeletionStack.push([=] { VK_CHECK(vkFreeDescriptorSets(gDevice, gDescriptorPool, 1, &gDescriptorSet)); });
}

void InitPipeline()
{
  auto shaderModule = LoadShaderModule("shaders/test.comp.spv").value_or(VkShaderModule{});
  gDeletionStack.push([=] { vkDestroyShaderModule(gDevice, shaderModule, nullptr); });

  auto shaderStageInfo = VkPipelineShaderStageCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = shaderModule,
    .pName = "main"
  };

  auto pipelineLayoutInfo = VkPipelineLayoutCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &gDescriptorSetLayout,
  };
  VK_CHECK(vkCreatePipelineLayout(gDevice, &pipelineLayoutInfo, nullptr, &gPipelineLayout));
  gDeletionStack.push([=] { vkDestroyPipelineLayout(gDevice, gPipelineLayout, nullptr); });

  auto pipelineInfo = VkComputePipelineCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = shaderStageInfo,
    .layout = gPipelineLayout,
  };
  VK_CHECK(vkCreateComputePipelines(gDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gPipeline));
  gDeletionStack.push([=] { vkDestroyPipeline(gDevice, gPipeline, nullptr); });
}

void InitCommandBuffers()
{
  auto poolInfo = VkCommandPoolCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = gQueueFamilyIndex
  };
  VK_CHECK(vkCreateCommandPool(gDevice, &poolInfo, nullptr, &gCommandPool));
  gDeletionStack.push([=] { vkDestroyCommandPool(gDevice, gCommandPool, nullptr); });

  auto allocCommandBufferInfo = VkCommandBufferAllocateInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = gCommandPool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };
  VK_CHECK(vkAllocateCommandBuffers(gDevice, &allocCommandBufferInfo, &gCommandBuffer));
}

void InitSync()
{
  auto fenceInfo = VkFenceCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
  };
  VK_CHECK(vkCreateFence(gDevice, &fenceInfo, nullptr, &gFence));
  gDeletionStack.push([=] { vkDestroyFence(gDevice, gFence, nullptr); });
}

void InitQueryPool()
{
  auto queryPoolInfo = VkQueryPoolCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .queryType = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount = 2
  };
  VK_CHECK(vkCreateQueryPool(gDevice, &queryPoolInfo, nullptr, &gQueryPool));
  gDeletionStack.push([=] { vkDestroyQueryPool(gDevice, gQueryPool, nullptr); });

  vkResetQueryPool(gDevice, gQueryPool, 0, 2);
}

void Init()
{
  InitVulkan();
  InitDescriptors();
  InitPipeline();
  InitCommandBuffers();
  InitSync();
  InitQueryPool();
}

void Deinit()
{
  while (!gDeletionStack.empty())
  {
    auto& fn = gDeletionStack.top();
    fn();
    gDeletionStack.pop();
  }
}

uint64_t SubmitAndGetTime(const std::function<void(VkCommandBuffer)>& fn)
{
  auto commandBufferBeginInfo = VkCommandBufferBeginInfo
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
  };

  VK_CHECK(vkBeginCommandBuffer(gCommandBuffer, &commandBufferBeginInfo));

  // issue timestamp as 
  vkCmdWriteTimestamp(gCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gQueryPool, 0);

  fn(gCommandBuffer);

  // issue timestamp when previous commands retire
  vkCmdWriteTimestamp(gCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, gQueryPool, 1);

  VK_CHECK(vkEndCommandBuffer(gCommandBuffer));

  auto submitInfo = VkSubmitInfo
  {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .commandBufferCount = 1,
    .pCommandBuffers = &gCommandBuffer
  };
  VK_CHECK(vkQueueSubmit(gQueue, 1, &submitInfo, gFence));
  VK_CHECK(vkWaitForFences(gDevice, 1, &gFence, true, 10'000'000'000));
  VK_CHECK(vkResetFences(gDevice, 1, &gFence));
  VK_CHECK(vkResetCommandPool(gDevice, gCommandPool, 0));
  
  uint64_t queryResults[2];
  VK_CHECK(vkGetQueryPoolResults(gDevice, gQueryPool,
    0, 2,
    sizeof(queryResults), queryResults, sizeof(uint64_t),
    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

  vkResetQueryPool(gDevice, gQueryPool, 0, 2);

  return queryResults[1] - queryResults[0];
}

BufferAllocation CreateBuffer(size_t sizeBytes, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags = 0, VkMemoryPropertyFlags memoryProperties = 0)
{
  auto bufferInfo = VkBufferCreateInfo
  {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = sizeBytes,
    .usage = usage
  };

  auto allocInfo = VmaAllocationCreateInfo
  {
    .flags = allocationFlags,
    .usage = VMA_MEMORY_USAGE_AUTO,
    .requiredFlags = memoryProperties
  };

  BufferAllocation buffer;
  VK_CHECK(vmaCreateBuffer(gAllocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, nullptr));
  return buffer;
}

int main()
{
  Init();

  const size_t bufferSize = 1024 * sizeof(uint32_t);

  auto buffer = CreateBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  gDeletionStack.push([=] { vmaDestroyBuffer(gAllocator, buffer.buffer, buffer.allocation); });
  auto hostBuffer = CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // for mapping
  gDeletionStack.push([=] { vmaDestroyBuffer(gAllocator, hostBuffer.buffer, hostBuffer.allocation); });

  // update descriptor set with our buffer
  auto descriptorBufferInfo = VkDescriptorBufferInfo
  {
    .buffer = buffer.buffer,
    .offset = 0,
    .range = bufferSize
  };
  auto descriptorWrite = VkWriteDescriptorSet
  {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = gDescriptorSet,
    .dstBinding = 0,
    .dstArrayElement = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .pBufferInfo = &descriptorBufferInfo
  };
  vkUpdateDescriptorSets(gDevice, 1, &descriptorWrite, 0, nullptr);

  auto result = SubmitAndGetTime([=](VkCommandBuffer cmdBuf)
    {
      uint32_t dispatchSize = 1024 / 64;
      vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, gPipelineLayout, 0, 1, &gDescriptorSet, 0, nullptr);
      vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, gPipeline);
      vkCmdDispatch(cmdBuf, dispatchSize, 1, 1);
    }); // do nothing
  
  std::cout << "Event time: " << gTimestampPeriod << " * " << result << " ns\n";

  SubmitAndGetTime([=](VkCommandBuffer cmdBuf)
    {
      auto region = VkBufferCopy
      {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = bufferSize
      };
      vkCmdCopyBuffer(cmdBuf, buffer.buffer, hostBuffer.buffer, 1, &region);
    });

  uint32_t* pData;
  VK_CHECK(vmaMapMemory(gAllocator, hostBuffer.allocation, reinterpret_cast<void**>(&pData)));
  for (int i = 0; i < 1024; i++)
  {
    std::cout << pData[i] << ' ';
  }
  vmaUnmapMemory(gAllocator, hostBuffer.allocation);

  Deinit();
  return 0;
}