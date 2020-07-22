#include "pch.h"

#include "Vulkan.h"

#include <kinc/graphics5/commandlist.h>
#include <kinc/graphics5/indexbuffer.h>
#include <kinc/graphics5/pipeline.h>
#include <kinc/graphics5/vertexbuffer.h>
#include <kinc/log.h>
#include <kinc/system.h>

#include <assert.h>
#include <memory.h>

#include <vulkan/vulkan.h>

extern VkDevice device;
extern VkCommandPool cmd_pool;
extern PFN_vkQueuePresentKHR fpQueuePresentKHR;
extern PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
extern VkSwapchainKHR swapchain;
extern VkQueue queue;
extern VkFramebuffer *framebuffers;
extern VkRenderPass render_pass;
extern VkDescriptorSet desc_set;
extern uint32_t current_buffer;
extern VkDescriptorPool desc_pools[3];

namespace {
	VkCommandBuffer setup_cmd;
	bool began = false;
	bool onBackBuffer = false;
	uint32_t lastVertexConstantBufferOffset = 0;
	uint32_t lastFragmentConstantBufferOffset = 0;
	kinc_g5_pipeline_t *currentPipeline = NULL;
	bool mrtFramebufferCreated = false;
	VkFramebuffer mrtFramebuffer;
	VkRenderPass mrtRenderPass;
}

void Kore::Vulkan::demo_set_image_layout(VkImage image, VkImageAspectFlags aspectMask, VkImageLayout old_image_layout, VkImageLayout new_image_layout) {
	VkResult err;
	if (setup_cmd == VK_NULL_HANDLE) {
		VkCommandBufferAllocateInfo cmd = {};
		cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd.pNext = nullptr;
		cmd.commandPool = cmd_pool;
		cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd.commandBufferCount = 1;

		err = vkAllocateCommandBuffers(device, &cmd, &setup_cmd);
		assert(!err);

		VkCommandBufferInheritanceInfo cmd_buf_hinfo = {};
		cmd_buf_hinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		cmd_buf_hinfo.pNext = nullptr;
		cmd_buf_hinfo.renderPass = VK_NULL_HANDLE;
		cmd_buf_hinfo.subpass = 0;
		cmd_buf_hinfo.framebuffer = VK_NULL_HANDLE;
		cmd_buf_hinfo.occlusionQueryEnable = VK_FALSE;
		cmd_buf_hinfo.queryFlags = 0;
		cmd_buf_hinfo.pipelineStatistics = 0;

		VkCommandBufferBeginInfo cmd_buf_info = {};
		cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmd_buf_info.pNext = nullptr;
		cmd_buf_info.flags = 0;
		cmd_buf_info.pInheritanceInfo = &cmd_buf_hinfo;

		err = vkBeginCommandBuffer(setup_cmd, &cmd_buf_info);
		assert(!err);
	}

	VkImageMemoryBarrier image_memory_barrier = {};
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.pNext = nullptr;
	image_memory_barrier.srcAccessMask = 0;
	image_memory_barrier.dstAccessMask = 0;
	image_memory_barrier.oldLayout = old_image_layout;
	image_memory_barrier.newLayout = new_image_layout;
	image_memory_barrier.image = image;
	image_memory_barrier.subresourceRange.aspectMask = aspectMask;
	image_memory_barrier.subresourceRange.baseMipLevel = 0;
	image_memory_barrier.subresourceRange.levelCount = 1;
	image_memory_barrier.subresourceRange.baseArrayLayer = 0;
	image_memory_barrier.subresourceRange.layerCount = 1;

	if (old_image_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
		image_memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		// Make sure anything that was copying from this image has completed
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

	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	if (new_image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	if (new_image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}

	vkCmdPipelineBarrier(setup_cmd, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
}

void Kore::Vulkan::demo_flush_init_cmd() {
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

extern VkSemaphore presentCompleteSemaphore;

void kinc_g5_command_list_init(kinc_g5_command_list_t *list) {
	VkCommandBufferAllocateInfo cmd = {};
	cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd.pNext = NULL;
	cmd.commandPool = cmd_pool;
	cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd.commandBufferCount = 1;

	VkResult err = vkAllocateCommandBuffers(device, &cmd, &list->impl._buffer);
	assert(!err);

	list->impl._indexCount = 0;

	// begin();
}

void kinc_g5_command_list_destroy(kinc_g5_command_list_t *list) {}

void kinc_g5_command_list_begin(kinc_g5_command_list_t *list) {
	// if (began) return;

	// Assume the command buffer has been run on current_buffer before so
	// we need to set the image layout back to COLOR_ATTACHMENT_OPTIMAL
	// demo_set_image_layout(Vulkan::buffers[current_buffer].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	// VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL); demo_flush_init_cmd();

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

	VkClearValue clear_values[2];
	memset(clear_values, 0, sizeof(VkClearValue) * 2);
	clear_values[0].color.float32[0] = 0.0f;
	clear_values[0].color.float32[1] = 0.0f;
	clear_values[0].color.float32[2] = 0.0f;
	clear_values[0].color.float32[3] = 1.0f;
	clear_values[1].depthStencil.depth = 1.0;
	clear_values[1].depthStencil.stencil = 0;

	VkRenderPassBeginInfo rp_begin = {};
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.pNext = NULL;
	rp_begin.renderPass = render_pass;
	rp_begin.framebuffer = framebuffers[current_buffer];
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = kinc_width();
	rp_begin.renderArea.extent.height = kinc_height();
	rp_begin.clearValueCount = 2;
	rp_begin.pClearValues = clear_values;

	VkResult err = vkBeginCommandBuffer(list->impl._buffer, &cmd_buf_info);
	assert(!err);

	VkImageMemoryBarrier prePresentBarrier = {};
	prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	prePresentBarrier.pNext = NULL;
	prePresentBarrier.srcAccessMask = 0;
	prePresentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	prePresentBarrier.image = Kore::Vulkan::buffers[current_buffer].image;
	VkImageMemoryBarrier *pmemory_barrier = &prePresentBarrier;
	vkCmdPipelineBarrier(list->impl._buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
						 pmemory_barrier);

	vkCmdBeginRenderPass(list->impl._buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	memset(&viewport, 0, sizeof(viewport));
	viewport.x = 0;
	viewport.y = (float)kinc_height();
	viewport.width = (float)kinc_width();
	viewport.height = -(float)kinc_height();
	viewport.minDepth = (float)0.0f;
	viewport.maxDepth = (float)1.0f;
	vkCmdSetViewport(list->impl._buffer, 0, 1, &viewport);

	VkRect2D scissor;
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = kinc_width();
	scissor.extent.height = kinc_height();
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor(list->impl._buffer, 0, 1, &scissor);

	vkResetDescriptorPool(device, desc_pools[current_buffer], 0);

	began = true;
	onBackBuffer = true;
}

void kinc_g5_command_list_end(kinc_g5_command_list_t *list) {
	vkCmdEndRenderPass(list->impl._buffer);

	VkImageMemoryBarrier prePresentBarrier = {};
	prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	prePresentBarrier.pNext = NULL;
	prePresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	prePresentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	prePresentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	prePresentBarrier.image = Kore::Vulkan::buffers[current_buffer].image;
	VkImageMemoryBarrier *pmemory_barrier = &prePresentBarrier;
	vkCmdPipelineBarrier(list->impl._buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, pmemory_barrier);

	VkResult err = vkEndCommandBuffer(list->impl._buffer);
	assert(!err);

	VkFence nullFence = VK_NULL_HANDLE;
	VkPipelineStageFlags pipe_stage_flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &presentCompleteSemaphore;
	submit_info.pWaitDstStageMask = &pipe_stage_flags;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &list->impl._buffer;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = NULL;

	err = vkQueueSubmit(queue, 1, &submit_info, nullFence);
	assert(!err);

	VkPresentInfoKHR present = {};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.pNext = NULL;
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &current_buffer;

	err = fpQueuePresentKHR(queue, &present);
	err = vkQueueWaitIdle(queue);
	assert(err == VK_SUCCESS);

	vkDestroySemaphore(device, presentCompleteSemaphore, NULL);

	vkResetCommandBuffer(list->impl._buffer, 0);

#ifndef KORE_WINDOWS
	vkDeviceWaitIdle(device);
#endif

	began = false;
}

void kinc_g5_command_list_clear(kinc_g5_command_list_t *list, struct kinc_g5_render_target *renderTarget, unsigned flags, unsigned color, float depth,
								int stencil) {
	VkClearRect clearRect = {};
	clearRect.rect.offset.x = 0;
	clearRect.rect.offset.y = 0;
	clearRect.rect.extent.width = renderTarget->width;
	clearRect.rect.extent.height = renderTarget->height;
	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;

	if (flags & KINC_G5_CLEAR_COLOR) {
		VkClearColorValue clearColor = {};
		clearColor.float32[0] = ((color & 0x00ff0000) >> 16) / 255.0f;
		clearColor.float32[1] = ((color & 0x0000ff00) >> 8) / 255.0f;
		clearColor.float32[2] = (color & 0x000000ff) / 255.0f;
		clearColor.float32[3] = ((color & 0xff000000) >> 24) / 255.0f;
		VkClearAttachment attachment = {};
		attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.colorAttachment = 0;
		attachment.clearValue.color = clearColor;
		vkCmdClearAttachments(list->impl._buffer, 1, &attachment, 1, &clearRect);
	}
	if ((flags & KINC_G5_CLEAR_DEPTH) || (flags & KINC_G5_CLEAR_STENCIL)) {
		VkClearAttachment attachment = {};
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		attachment.clearValue.depthStencil.depth = depth;
		attachment.clearValue.depthStencil.stencil = stencil;
		vkCmdClearAttachments(list->impl._buffer, 1, &attachment, 1, &clearRect);
	}
}

void kinc_g5_command_list_render_target_to_framebuffer_barrier(kinc_g5_command_list_t *list, struct kinc_g5_render_target *renderTarget) {}

void kinc_g5_command_list_framebuffer_to_render_target_barrier(kinc_g5_command_list_t *list, struct kinc_g5_render_target *renderTarget) {}

void kinc_g5_command_list_draw_indexed_vertices(kinc_g5_command_list_t *list) {
	kinc_g5_command_list_draw_indexed_vertices_from_to(list, 0, list->impl._indexCount);
}

void kinc_g5_command_list_draw_indexed_vertices_from_to(kinc_g5_command_list_t *list, int start, int count) {
	vkCmdDrawIndexed(list->impl._buffer, count, 1, start, 0, 0);
}

void kinc_g5_command_list_draw_indexed_vertices_from_to_from(kinc_g5_command_list_t *list, int start, int count, int vertex_offset) {
	vkCmdDrawIndexed(list->impl._buffer, count, 1, start, vertex_offset, 0);
}

void kinc_g5_command_list_viewport(kinc_g5_command_list_t *list, int x, int y, int width, int height) {}

void kinc_g5_command_list_scissor(kinc_g5_command_list_t *list, int x, int y, int width, int height) {}

void kinc_g5_command_list_disable_scissor(kinc_g5_command_list_t *list) {}

void kinc_g5_command_list_set_pipeline(kinc_g5_command_list_t *list, struct kinc_g5_pipeline *pipeline) {
	currentPipeline = pipeline;
	lastVertexConstantBufferOffset = 0;
	lastFragmentConstantBufferOffset = 0;

	vkCmdBindPipeline(list->impl._buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->impl.pipeline);
}

void kinc_g5_command_list_set_vertex_buffers(kinc_g5_command_list_t *list, struct kinc_g5_vertex_buffer **vertexBuffers, int *offsets_, int count) {
	kinc_g5_internal_vertex_buffer_set(vertexBuffers[0], 0);
	VkDeviceSize offsets[1] = {(uint64_t)(offsets_[0] * kinc_g5_vertex_buffer_stride(vertexBuffers[0]))};
	vkCmdBindVertexBuffers(list->impl._buffer, 0, 1, &vertexBuffers[0]->impl.vertices.buf, offsets);
}

void kinc_g5_command_list_set_index_buffer(kinc_g5_command_list_t *list, struct kinc_g5_index_buffer *indexBuffer) {
	list->impl._indexCount = kinc_g5_index_buffer_count(indexBuffer);
	vkCmdBindIndexBuffer(list->impl._buffer, indexBuffer->impl.buf, 0, VK_INDEX_TYPE_UINT32);
}

void setImageLayout(VkCommandBuffer _buffer, VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout);

namespace {
	kinc_g5_render_target_t *currentRenderTargets[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

	void endPass(VkCommandBuffer _buffer) {
		vkCmdEndRenderPass(_buffer);

		if (currentRenderTargets[0] == nullptr || currentRenderTargets[0]->contextId < 0) {
			/*VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.pNext = NULL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			barrier.image = buffers[current_buffer].image;

			vkCmdPipelineBarrier(draw_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);*/
		}
		else {
			int i = 0;
			while (currentRenderTargets[i] != nullptr) {
				setImageLayout(_buffer, currentRenderTargets[i]->impl.sourceImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
							   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
				setImageLayout(_buffer, currentRenderTargets[i]->impl.destImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

				VkImageBlit imgBlit;

				imgBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imgBlit.srcSubresource.mipLevel = 0;
				imgBlit.srcSubresource.baseArrayLayer = 0;
				imgBlit.srcSubresource.layerCount = 1;

				imgBlit.srcOffsets[0] = {0, 0, 0};
				imgBlit.srcOffsets[1].x = currentRenderTargets[i]->width;
				imgBlit.srcOffsets[1].y = currentRenderTargets[i]->height;
				imgBlit.srcOffsets[1].z = 1;

				imgBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imgBlit.dstSubresource.mipLevel = 0;
				imgBlit.dstSubresource.baseArrayLayer = 0;
				imgBlit.dstSubresource.layerCount = 1;

				imgBlit.dstOffsets[0] = {0, 0, 0};
				imgBlit.dstOffsets[1].x = currentRenderTargets[i]->width;
				imgBlit.dstOffsets[1].y = currentRenderTargets[i]->height;
				imgBlit.dstOffsets[1].z = 1;

				vkCmdBlitImage(_buffer, currentRenderTargets[i]->impl.sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentRenderTargets[i]->impl.destImage,
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgBlit, VK_FILTER_LINEAR);

				setImageLayout(_buffer, currentRenderTargets[i]->impl.sourceImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
							   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				setImageLayout(_buffer, currentRenderTargets[i]->impl.destImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				i++;
			}
		}

		/*VkResult err = vkEndCommandBuffer(draw_cmd);
		assert(!err);

		VkFence nullFence = VK_NULL_HANDLE;
		VkPipelineStageFlags pipe_stage_flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.pNext = NULL;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &presentCompleteSemaphore;
		submit_info.pWaitDstStageMask = &pipe_stage_flags;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &draw_cmd;
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;

		err = vkQueueSubmit(queue, 1, &submit_info, nullFence);
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

		VkClearValue clear_values[2];
		memset(clear_values, 0, sizeof(VkClearValue) * 2);
		clear_values[0].color.float32[0] = 0.0f;
		clear_values[0].color.float32[1] = 0.0f;
		clear_values[0].color.float32[2] = 0.0f;
		clear_values[0].color.float32[3] = 1.0f;
		clear_values[1].depthStencil.depth = 1.0;
		clear_values[1].depthStencil.stencil = 0;

		VkRenderPassBeginInfo rp_begin = {};
		rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rp_begin.pNext = NULL;
		rp_begin.renderPass = render_pass;
		rp_begin.framebuffer = framebuffers[current_buffer];
		rp_begin.renderArea.offset.x = 0;
		rp_begin.renderArea.offset.y = 0;
		rp_begin.renderArea.extent.width = width;
		rp_begin.renderArea.extent.height = height;
		rp_begin.clearValueCount = 2;
		rp_begin.pClearValues = clear_values;

		err = vkBeginCommandBuffer(draw_cmd, &cmd_buf_info);
		assert(!err);*/
	}
}

void kinc_internal_restore_render_target(kinc_g5_command_list_t *list, struct kinc_g5_render_target *target) {
	if (onBackBuffer) return;
	endPass(list->impl._buffer);
	currentRenderTargets[0] = nullptr;
	onBackBuffer = true;
	VkClearValue clear_values[2];
	memset(clear_values, 0, sizeof(VkClearValue) * 2);
	clear_values[0].color.float32[0] = 0.0f;
	clear_values[0].color.float32[1] = 0.0f;
	clear_values[0].color.float32[2] = 0.0f;
	clear_values[0].color.float32[3] = 1.0f;
	clear_values[1].depthStencil.depth = 1.0;
	clear_values[1].depthStencil.stencil = 0;
	VkRenderPassBeginInfo rp_begin = {};
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.pNext = nullptr;
	rp_begin.renderPass = render_pass;
	rp_begin.framebuffer = framebuffers[current_buffer];
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = kinc_width();
	rp_begin.renderArea.extent.height = kinc_height();
	rp_begin.clearValueCount = 2;
	rp_begin.pClearValues = clear_values;
	vkCmdBeginRenderPass(list->impl._buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport viewport;
	memset(&viewport, 0, sizeof(viewport));
	viewport.x = 0;
	viewport.y = (float)kinc_height();
	viewport.width = (float)kinc_width();
	viewport.height = -(float)kinc_height();
	viewport.minDepth = (float)0.0f;
	viewport.maxDepth = (float)1.0f;
	vkCmdSetViewport(list->impl._buffer, 0, 1, &viewport);
	VkRect2D scissor;
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = kinc_width();
	scissor.extent.height = kinc_height();
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor(list->impl._buffer, 0, 1, &scissor);
}

void kinc_g5_command_list_set_render_targets(kinc_g5_command_list_t *list, struct kinc_g5_render_target **targets, int count) {

	if (targets[0]->contextId < 0) {
		kinc_internal_restore_render_target(list, targets[0]);
		return;
	}

	endPass(list->impl._buffer);
	for (int i = 0; i < count; ++i) {
		currentRenderTargets[i] = targets[i];
	}
	onBackBuffer = false;

	VkClearValue clear_values[9];
	memset(clear_values, 0, sizeof(VkClearValue));
	for (int i = 0; i < count; ++i) {
		clear_values[i].color.float32[0] = 0.0f;
		clear_values[i].color.float32[1] = 0.0f;
		clear_values[i].color.float32[2] = 0.0f;
		clear_values[i].color.float32[3] = 1.0f;
	}
	clear_values[count].depthStencil.depth = 1.0;
	clear_values[count].depthStencil.stencil = 0;

	VkRenderPassBeginInfo rp_begin = {};
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.pNext = nullptr;
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = targets[0]->width;
	rp_begin.renderArea.extent.height = targets[0]->height;
	rp_begin.clearValueCount = count + 1;
	rp_begin.pClearValues = clear_values;

	if (count == 1) {
		rp_begin.renderPass = targets[0]->impl.renderPass;
		rp_begin.framebuffer = targets[0]->impl.framebuffer;
	}
	else {
		if (mrtFramebufferCreated) {
			vkDestroyFramebuffer(device, mrtFramebuffer, nullptr);
			vkDestroyRenderPass(device, mrtRenderPass, nullptr);
		}

		VkAttachmentDescription attachments[9];
		for (int i = 0; i < count; ++i) {
			attachments[i].format = VK_FORMAT_B8G8R8A8_UNORM;
			attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[i].flags = 0;
		}
		attachments[count].format = VK_FORMAT_D16_UNORM;
		attachments[count].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[count].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[count].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[count].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[count].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[count].flags = 0;

		VkAttachmentReference color_references[8];
		for (int i = 0; i < count; ++i) {
			color_references[i].attachment = i;
			color_references[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		VkAttachmentReference depth_reference = {};
		depth_reference.attachment = count;
		depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.flags = 0;
		subpass.inputAttachmentCount = 0;
		subpass.pInputAttachments = nullptr;
		subpass.colorAttachmentCount = count;
		subpass.pColorAttachments = color_references;
		subpass.pResolveAttachments = nullptr;
		subpass.pDepthStencilAttachment = &depth_reference;
		subpass.preserveAttachmentCount = 0;
		subpass.pPreserveAttachments = nullptr;

		VkRenderPassCreateInfo rp_info = {};
		rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rp_info.pNext = nullptr;
		rp_info.attachmentCount = count + 1;
		rp_info.pAttachments = attachments;
		rp_info.subpassCount = 1;
		rp_info.pSubpasses = &subpass;
		rp_info.dependencyCount = 0;
		rp_info.pDependencies = nullptr;

		VkResult err = vkCreateRenderPass(device, &rp_info, NULL, &mrtRenderPass);
		assert(!err);

		VkImageView attachmentsViews[9];
		for (int i = 0; i < count; ++i) {
			attachmentsViews[i] = targets[i]->impl.sourceView;
		}
		attachmentsViews[count] = targets[0]->impl.depthView;

		VkFramebufferCreateInfo fbufCreateInfo = {};
		fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo.pNext = nullptr;
		fbufCreateInfo.renderPass = mrtRenderPass;
		fbufCreateInfo.attachmentCount = count + 1;
		fbufCreateInfo.pAttachments = attachmentsViews;
		fbufCreateInfo.width = targets[0]->width;
		fbufCreateInfo.height = targets[0]->height;
		fbufCreateInfo.layers = 1;

		err = vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &mrtFramebuffer);
		assert(!err);
		mrtFramebufferCreated = true;

		rp_begin.renderPass = mrtRenderPass;
		rp_begin.framebuffer = mrtFramebuffer;
	}

	vkCmdBeginRenderPass(list->impl._buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	memset(&viewport, 0, sizeof(viewport));
	viewport.x = 0;
	viewport.y = (float)targets[0]->height;
	viewport.width = (float)targets[0]->width;
	viewport.height = -(float)targets[0]->height;
	viewport.minDepth = (float)0.0f;
	viewport.maxDepth = (float)1.0f;
	vkCmdSetViewport(list->impl._buffer, 0, 1, &viewport);

	VkRect2D scissor;
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = targets[0]->width;
	scissor.extent.height = targets[0]->height;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor(list->impl._buffer, 0, 1, &scissor);
}

void kinc_g5_command_list_upload_index_buffer(kinc_g5_command_list_t *list, struct kinc_g5_index_buffer *buffer) {}

void kinc_g5_command_list_upload_vertex_buffer(kinc_g5_command_list_t *list, struct kinc_g5_vertex_buffer *buffer) {}

void kinc_g5_command_list_upload_texture(kinc_g5_command_list_t *list, struct kinc_g5_texture *texture) {}

void kinc_g5_command_list_get_render_target_pixels(kinc_g5_command_list_t *list, kinc_g5_render_target_t *render_target, uint8_t *data) {}

void kinc_g5_command_list_texture_to_render_target_barrier(kinc_g5_command_list_t *list, struct kinc_g5_render_target *renderTarget) {}

void kinc_g5_command_list_render_target_to_texture_barrier(kinc_g5_command_list_t *list, struct kinc_g5_render_target *renderTarget) {}

void kinc_g5_command_list_set_vertex_constant_buffer(kinc_g5_command_list_t *list, struct kinc_g5_constant_buffer *buffer, int offset, size_t size) {
	lastVertexConstantBufferOffset = offset;
}

void kinc_g5_command_list_set_fragment_constant_buffer(kinc_g5_command_list_t *list, struct kinc_g5_constant_buffer *buffer, int offset, size_t size) {
	lastFragmentConstantBufferOffset = offset;

	Kore::Vulkan::createDescriptorSet(&currentPipeline->impl, desc_set);
	uint32_t offsets[2] = {lastVertexConstantBufferOffset, lastFragmentConstantBufferOffset};
	vkCmdBindDescriptorSets(list->impl._buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->impl.pipeline_layout, 0, 1, &desc_set, 2, offsets);
}

void kinc_g5_command_list_set_pipeline_layout(kinc_g5_command_list_t *list) {}

void kinc_g5_command_list_execute(kinc_g5_command_list_t *list) {}

void kinc_g5_command_list_execute_and_wait(kinc_g5_command_list_t *list) {}
