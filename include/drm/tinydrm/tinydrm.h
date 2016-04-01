/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_TINYDRM_H
#define __LINUX_TINYDRM_H

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_simple_kms_helper.h>

struct tinydrm_deferred;
struct tinydrm_fbdev;
struct spi_device;
struct regulator;
struct lcdreg;

struct tinydrm_device {
	struct drm_device *base;
	u32 width, height;
	struct drm_simple_display_pipe pipe;
	struct drm_panel panel;
	struct tinydrm_fbdev *fbdev;
	struct tinydrm_deferred *deferred;
	struct backlight_device *backlight;
	struct regulator *regulator;
	struct lcdreg *lcdreg;
	bool prepared;
	bool enabled;
	void *dev_private;

	int (*dirtyfb)(struct drm_framebuffer *fb, void *vmem, unsigned flags,
		       unsigned color, struct drm_clip_rect *clips,
		       unsigned num_clips);
};

extern const struct file_operations tinydrm_fops;
void tinydrm_lastclose(struct drm_device *dev);

#define TINYDRM_DRM_DRIVER(name_struct, name_str, desc_str, date_str) \
static struct drm_driver name_struct = { \
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME \
				| DRIVER_ATOMIC, \
	.lastclose		= tinydrm_lastclose, \
	.gem_free_object	= drm_gem_cma_free_object, \
	.gem_vm_ops		= &drm_gem_cma_vm_ops, \
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd, \
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle, \
	.gem_prime_import	= drm_gem_prime_import, \
	.gem_prime_export	= drm_gem_prime_export, \
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table, \
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table, \
	.gem_prime_vmap		= drm_gem_cma_prime_vmap, \
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap, \
	.gem_prime_mmap		= drm_gem_cma_prime_mmap, \
	.dumb_create		= drm_gem_cma_dumb_create, \
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset, \
	.dumb_destroy		= drm_gem_dumb_destroy, \
	.fops			= &tinydrm_fops, \
	.name			= name_str, \
	.desc			= desc_str, \
	.date			= date_str, \
	.major			= 1, \
	.minor			= 0, \
}

struct drm_framebuffer *tinydrm_fb_cma_dumb_create(struct drm_device *dev,
					struct drm_file *file_priv,
					const struct drm_mode_fb_cmd2 *mode_cmd);
int tinydrm_display_pipe_init(struct tinydrm_device *tdev,
			      const uint32_t *formats, unsigned int format_count);
int tinydrm_panel_get_modes(struct drm_panel *panel);
int devm_tinydrm_register(struct device *dev, struct tinydrm_device *tdev,
			  struct drm_driver *driver);

static inline struct tinydrm_device *tinydrm_from_panel(struct drm_panel *panel)
{
	return panel->connector->dev->dev_private;
}

static inline void tinydrm_prepare(struct tinydrm_device *tdev)
{
	if (!tdev->prepared) {
		drm_panel_prepare(&tdev->panel);
		tdev->prepared = true;
	}
}

static inline void tinydrm_unprepare(struct tinydrm_device *tdev)
{
	if (tdev->prepared) {
		drm_panel_unprepare(&tdev->panel);
		tdev->prepared = false;
	}
}

static inline void tinydrm_enable(struct tinydrm_device *tdev)
{
	if (!tdev->enabled) {
		drm_panel_enable(&tdev->panel);
		tdev->enabled = true;
	}
}

static inline void tinydrm_disable(struct tinydrm_device *tdev)
{
	if (tdev->enabled) {
		drm_panel_disable(&tdev->panel);
		tdev->enabled = false;
	}
}

#ifdef CONFIG_DRM_KMS_FB_HELPER
int tinydrm_fbdev_init(struct tinydrm_device *tdev);
void tinydrm_fbdev_fini(struct tinydrm_device *tdev);
void tinydrm_fbdev_restore_mode(struct tinydrm_fbdev *fbdev);
#else
static inline int tinydrm_fbdev_init(struct tinydrm_device *tdev)
{
	return 0;
}

static inline void tinydrm_fbdev_fini(struct tinydrm_device *tdev)
{
}

static inline void tinydrm_fbdev_restore_mode(struct tinydrm_fbdev *fbdev)
{
}
#endif

struct backlight_device *tinydrm_of_find_backlight(struct device *dev);
int tinydrm_panel_enable_backlight(struct drm_panel *panel);
int tinydrm_panel_disable_backlight(struct drm_panel *panel);
extern const struct dev_pm_ops tinydrm_simple_pm_ops;
void tinydrm_spi_shutdown(struct spi_device *spi);

struct tinydrm_fb_clip {
	struct drm_framebuffer *fb;
	struct drm_clip_rect clip;
	void *vmem;
};

struct tinydrm_deferred {
	struct delayed_work dwork;
	struct tinydrm_fb_clip fb_clip;
	unsigned defer_ms;
	spinlock_t lock;
	bool no_delay;
};

static inline struct tinydrm_device *work_to_tinydrm(struct work_struct *work)
{
	struct tinydrm_deferred *deferred;

	deferred = container_of(work, struct tinydrm_deferred, dwork.work);
	return deferred->fb_clip.fb->dev->dev_private;
}

bool tinydrm_deferred_begin(struct tinydrm_device *tdev,
			    struct tinydrm_fb_clip *fb_clip);
void tinydrm_deferred_end(struct tinydrm_device *tdev);
int tinydrm_dirtyfb(struct drm_framebuffer *fb, void *vmem, unsigned flags,
		    unsigned color, struct drm_clip_rect *clips,
		    unsigned num_clips);

static inline bool tinydrm_is_full_clip(struct drm_clip_rect *clip, u32 width, u32 height)
{
	return clip->x1 == 0 && clip->x2 >= (width - 1) &&
	       clip->y1 == 0 && clip->y2 >= (height -1);
}

static inline void tinydrm_reset_clip(struct drm_clip_rect *clip)
{
	clip->x1 = ~0;
	clip->x2 = 0;
	clip->y1 = ~0;
	clip->y2 = 0;
}

void tinydrm_merge_clips(struct drm_clip_rect *dst,
			 struct drm_clip_rect *clips, unsigned num_clips,
			 unsigned flags, u32 width, u32 height);

#endif /* __LINUX_TINYDRM_H */
