#include "pch.h"

#include "Vulkan.h"

#include <kinc/image.h>
#include <kinc/graphics5/texture.h>
#include <kinc/log.h>

#include <assert.h>
#include <string.h>

#include <vulkan/vulkan.h>

extern VkDevice device;
extern VkPhysicalDevice gpu;
VkCommandBuffer setup_cmd;
extern VkCommandPool cmd_pool;
extern VkQueue queue;
extern bool use_staging_buffer;

bool memory_type_from_properties(uint32_t typeBits, VkFlags requirements_mask, uint32_t* typeIndex);

namespace {
	void demo_flush_init_cmd() {
		VkResult err;

		if (setup_cmd == VK_NULL_HANDLE) return;

		err = vkEndCommandBuffer(setup_cmd);
		assert(!err);

		const VkCommandBuffer cmd_bufs[] = {setup_cmd};
		VkFence nullFence = {VK_NULL_HANDLE};
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.pNext = NULL;
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = cmd_bufs;
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;

		err = vkQueueSubmit(queue, 1, &submit_info, nullFence);
		assert(!err);

		err = vkQueueWaitIdle(queue);
		assert(!err);

		vkFreeCommandBuffers(device, cmd_pool, 1, cmd_bufs);
		setup_cmd = VK_NULL_HANDLE;
	}

	void demo_set_image_layout(VkImage image, VkImageAspectFlags aspectMask, VkImageLayout old_image_layout, VkImageLayout new_image_layout) {
		VkResult err;

		if (setup_cmd == VK_NULL_HANDLE) {
			VkCommandBufferAllocateInfo cmd = {};
			cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cmd.pNext = NULL;
			cmd.commandPool = cmd_pool;
			cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cmd.commandBufferCount = 1;

			err = vkAllocateCommandBuffers(device, &cmd, &setup_cmd);
			assert(!err);

			VkCommandBufferInheritanceInfo cmd_buf_hinfo = {};
			cmd_buf_hinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
			cmd_buf_hinfo.pNext = NULL;
			cmd_buf_hinfo.renderPass = VK_NULL_HANDLE;
			cmd_buf_hinfo.subpass = 0;
			cmd_buf_hinfo.framebuffer = VK_NULL_HANDLE;
			cmd_buf_hinfo.occlusionQueryEnable = VK_FALSE;
			cmd_buf_hinfo.queryFlags = 0;
			cmd_buf_hinfo.pipelineStatistics = 0;

			VkCommandBufferBeginInfo cmd_buf_info = {};
			cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			cmd_buf_info.pNext = NULL;
			cmd_buf_info.flags = 0;
			cmd_buf_info.pInheritanceInfo = &cmd_buf_hinfo;

			err = vkBeginCommandBuffer(setup_cmd, &cmd_buf_info);
			assert(!err);
		}

		VkImageMemoryBarrier image_memory_barrier = {};
		image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier.pNext = NULL;
		image_memory_barrier.srcAccessMask = 0;
		image_memory_barrier.dstAccessMask = 0;
		image_memory_barrier.oldLayout = old_image_layout;
		image_memory_barrier.newLayout = new_image_layout;
		image_memory_barrier.image = image;
		image_memory_barrier.subresourceRange = {aspectMask, 0, 1, 0, 1};

		if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			/* Make sure anything that was copying from this image has completed */
			image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		}

		if (new_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			image_memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}

		if (new_image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			image_memory_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		if (new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			// Make sure any Copy or CPU writes to image are flushed
			image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		}

		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		vkCmdPipelineBarrier(setup_cmd, srcStageMask, dstStageMask, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);
	}

	void demo_prepare_texture_image(uint8_t *tex_colors, uint32_t tex_width, uint32_t tex_height, texture_object* tex_obj, VkImageTiling tiling,
	                                VkImageUsageFlags usage, VkFlags required_props, VkDeviceSize& deviceSize, VkFormat tex_format) {
		VkResult err;
		bool pass;

		tex_obj->tex_width = tex_width;
		tex_obj->tex_height = tex_height;

		VkImageCreateInfo image_create_info = {};
		image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_create_info.pNext = NULL;
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = tex_format;
		image_create_info.extent = {tex_width, tex_height, 1};
		image_create_info.mipLevels = 1;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.tiling = tiling;
		image_create_info.usage = usage;
		image_create_info.flags = 0;
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

		VkMemoryAllocateInfo mem_alloc = {};
		mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_alloc.pNext = NULL;
		mem_alloc.allocationSize = 0;
		mem_alloc.memoryTypeIndex = 0;

		VkMemoryRequirements mem_reqs;

		err = vkCreateImage(device, &image_create_info, NULL, &tex_obj->image);
		assert(!err);

		vkGetImageMemoryRequirements(device, tex_obj->image, &mem_reqs);

		deviceSize = mem_alloc.allocationSize = mem_reqs.size;
		pass = memory_type_from_properties(mem_reqs.memoryTypeBits, required_props, &mem_alloc.memoryTypeIndex);
		assert(pass);

		// allocate memory
		err = vkAllocateMemory(device, &mem_alloc, NULL, &tex_obj->mem);
		assert(!err);

		// bind memory
		err = vkBindImageMemory(device, tex_obj->image, tex_obj->mem, 0);
		assert(!err);

		if (required_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
			VkImageSubresource subres = {};
			subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subres.mipLevel = 0;
			subres.arrayLayer = 0;

			VkSubresourceLayout layout;
			uint8_t *data;

			vkGetImageSubresourceLayout(device, tex_obj->image, &subres, &layout);

			err = vkMapMemory(device, tex_obj->mem, 0, mem_alloc.allocationSize, 0, (void**)&data);
			assert(!err);

			for (uint32_t y = 0; y < tex_height; y++) {
				// uint32_t *row = (uint32_t *)((char *)data + layout.rowPitch * y);
				for (uint32_t x = 0; x < tex_width; x++) {
					data[y * layout.rowPitch + x * 4 + 0] = tex_colors[y * tex_width * 4 + x * 4 + 2];
					data[y * layout.rowPitch + x * 4 + 1] = tex_colors[y * tex_width * 4 + x * 4 + 1];
					data[y * layout.rowPitch + x * 4 + 2] = tex_colors[y * tex_width * 4 + x * 4 + 0];
					data[y * layout.rowPitch + x * 4 + 3] = tex_colors[y * tex_width * 4 + x * 4 + 3];
					// row[x] = tex_colors[(x & 1) ^ (y & 1)];
				}
			}

			vkUnmapMemory(device, tex_obj->mem);
		}

		tex_obj->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		demo_set_image_layout(tex_obj->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, tex_obj->imageLayout);
		// setting the image layout does not reference the actual memory so no need to add a mem ref
	}

	void demo_destroy_texture_image(texture_object* tex_obj) {
		// clean up staging resources
		vkDestroyImage(device, tex_obj->image, NULL);
		vkFreeMemory(device, tex_obj->mem, NULL);
	}
}

static VkFormat convert_format(kinc_image_format_t format) {
	switch (format) {
	case KINC_IMAGE_FORMAT_RGBA128:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case KINC_IMAGE_FORMAT_RGBA64:
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	case KINC_IMAGE_FORMAT_RGB24:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case KINC_IMAGE_FORMAT_A32:
		return VK_FORMAT_R32_SFLOAT;
	case KINC_IMAGE_FORMAT_A16:
		return VK_FORMAT_R16_SFLOAT;
	case KINC_IMAGE_FORMAT_GREY8:
		return VK_FORMAT_R8_UNORM;
	case KINC_IMAGE_FORMAT_BGRA32:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case KINC_IMAGE_FORMAT_RGBA32:
		return VK_FORMAT_B8G8R8A8_UNORM;
	default:
		return VK_FORMAT_B8G8R8A8_UNORM;
	}
}

void kinc_g5_texture_init_from_image(kinc_g5_texture_t *texture, kinc_image_t *image) {
	texture->texWidth = image->width;
	texture->texHeight = image->height;

	const VkFormat tex_format = convert_format(image->format);
	VkFormatProperties props;
	VkResult err;

	vkGetPhysicalDeviceFormatProperties(gpu, tex_format, &props);

	if ((props.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) && !use_staging_buffer) {
		// Device can texture using linear textures
		demo_prepare_texture_image((uint8_t*)image->data, (uint32_t)image->width, (uint32_t)image->height, &texture->impl.texture, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_SAMPLED_BIT,
		                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, texture->impl.deviceSize, tex_format);

		demo_flush_init_cmd();
	}
	else if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
		// Must use staging buffer to copy linear texture to optimized
		texture_object staging_texture;

		memset(&staging_texture, 0, sizeof(staging_texture));
		demo_prepare_texture_image((uint8_t*)image->data, (uint32_t)image->width, (uint32_t)image->height, &staging_texture, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, texture->impl.deviceSize, tex_format);
		demo_prepare_texture_image((uint8_t*)image->data, (uint32_t)image->width, (uint32_t)image->height, &texture->impl.texture, VK_IMAGE_TILING_OPTIMAL,
		                           (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		                           texture->impl.deviceSize, tex_format);
		demo_set_image_layout(staging_texture.image, VK_IMAGE_ASPECT_COLOR_BIT, staging_texture.imageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		demo_set_image_layout(texture->impl.texture.image, VK_IMAGE_ASPECT_COLOR_BIT, texture->impl.texture.imageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkImageCopy copy_region = {};
		copy_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy_region.srcOffset = {0, 0, 0};
		copy_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy_region.dstOffset = {0, 0, 0};
		copy_region.extent = {(uint32_t)staging_texture.tex_width, (uint32_t)staging_texture.tex_height, 1};

		vkCmdCopyImage(setup_cmd, staging_texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->impl.texture.image,
		               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
		               &copy_region);

		demo_set_image_layout(texture->impl.texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture->impl.texture.imageLayout);

		demo_flush_init_cmd();

		demo_destroy_texture_image(&staging_texture);
	}
	else {
		// Can't support VK_FORMAT_B8G8R8A8_UNORM !?
		assert(!"No support for B8G8R8A8_UNORM as texture image format");
	}

	VkSamplerCreateInfo sampler = {};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.pNext = NULL;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.mipLodBias = 0.0f;
	sampler.anisotropyEnable = VK_FALSE;
	sampler.maxAnisotropy = 1;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	sampler.maxLod = 0.0f;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	sampler.unnormalizedCoordinates = VK_FALSE;

	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.pNext = NULL;
	view.image = VK_NULL_HANDLE;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = tex_format;
	view.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
	view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	view.flags = 0;

	// create sampler
	err = vkCreateSampler(device, &sampler, NULL, &texture->impl.texture.sampler);
	assert(!err);

	// create image view
	view.image = texture->impl.texture.image;
	err = vkCreateImageView(device, &view, NULL, &texture->impl.texture.view);
	assert(!err);
}

void kinc_g5_texture_init(kinc_g5_texture_t* texture, int width, int height, kinc_image_format_t format) {

}

void kinc_g5_texture_init3d(kinc_g5_texture_t* texture, int width, int height, int depth, kinc_image_format_t format) {

}

void kinc_g5_texture_init_non_sampled_access(kinc_g5_texture_t *texture, int width, int height, kinc_image_format_t format) {

}

void kinc_g5_texture_destroy(kinc_g5_texture_t *texture) {}

void kinc_g5_internal_texture_set(kinc_g5_texture_t *texture, int unit) {}

int kinc_g5_texture_stride(kinc_g5_texture_t *texture) {
	return texture->texWidth * 4;
}

uint8_t *kinc_g5_texture_lock(kinc_g5_texture_t *texture) {
	VkImageSubresource subres = {};
	subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subres.mipLevel = 0;
	subres.arrayLayer = 0;

	VkSubresourceLayout layout;
	void* data;

	vkGetImageSubresourceLayout(device, texture->impl.texture.image, &subres, &layout);

	VkResult err = vkMapMemory(device, texture->impl.texture.mem, 0, texture->impl.deviceSize, 0, &data);
	assert(!err);
	//**return (u8*)data;
	return NULL;
}

void kinc_g5_texture_unlock(kinc_g5_texture_t *texture) {
	vkUnmapMemory(device, texture->impl.texture.mem);
}

void kinc_g5_texture_clear(kinc_g5_texture_t *texture, int x, int y, int z, int width, int height, int depth, unsigned color) {}

void kinc_g5_texture_generate_mipmaps(kinc_g5_texture_t *texture, int levels) {}

void kinc_g5_texture_set_mipmap(kinc_g5_texture_t *texture, kinc_g5_texture_t *mipmap, int level) {}
