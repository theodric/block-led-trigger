// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/fs.h>

static bool invert_brightness;
module_param(invert_brightness, bool, 0444);
MODULE_PARM_DESC(invert_brightness, "Invert LED brightness");

static unsigned long blink_delay_ms = 10;
module_param(blink_delay_ms, ulong, 0644);
MODULE_PARM_DESC(blink_delay_ms, "Duration of LED blink in milliseconds");

static bool debug_mode = false;
module_param(debug_mode, bool, 0644);
MODULE_PARM_DESC(debug_mode, "Enable debug output");

static unsigned long check_interval_ms = 100;
module_param(check_interval_ms, ulong, 0644);
MODULE_PARM_DESC(check_interval_ms, "Interval to check for disk activity (ms)");

static char *devices[8];
static int device_count;
module_param_array(devices, charp, &device_count, 0444);
MODULE_PARM_DESC(devices, "Block devices to monitor (e.g., sda,sdb)");

DEFINE_LED_TRIGGER(ledtrig_block);

static struct timer_list blink_timer;
static bool led_state = false;

static void blink_timer_callback(struct timer_list *t)
{
	led_state = !led_state;
	led_trigger_event(ledtrig_block, led_state ? LED_FULL : LED_OFF);
	
	if (led_state) {
		mod_timer(&blink_timer, jiffies + msecs_to_jiffies(blink_delay_ms));
	}
}

static void trigger_led_blink(void)
{
	if (!timer_pending(&blink_timer)) {
		led_state = true;
		led_trigger_event(ledtrig_block, LED_FULL);
		mod_timer(&blink_timer, jiffies + msecs_to_jiffies(blink_delay_ms));
	}
}

// Hook into the block layer by monitoring /proc/diskstats and /proc/vmstat
static struct workqueue_struct *activity_wq;
static struct delayed_work activity_work;
static unsigned long last_total_ios = 0;
static unsigned long last_dirty_pages = 0;

static void check_disk_activity(struct work_struct *work)
{
	struct file *f;
	loff_t pos = 0;
	char buf[2048];
	ssize_t len;
	unsigned long current_total_ios = 0;
	unsigned long total_reads = 0;
	unsigned long total_writes = 0;
	unsigned long current_dirty_pages = 0;
	int disk_count = 0;
	bool activity_detected = false;
	
	// Read /proc/diskstats to check for actual disk I/O
	f = filp_open("/proc/diskstats", O_RDONLY, 0);
	if (!IS_ERR(f)) {
		len = kernel_read(f, buf, sizeof(buf) - 1, &pos);
		if (len > 0) {
			buf[len] = '\0';
			
			// Parse the content line by line manually
			char *start = buf;
			char *end;
			
			while ((end = strchr(start, '\n')) != NULL) {
				*end = '\0';
				
				// Parse: major minor name rd_ios rd_merged rd_sectors rd_ticks wr_ios wr_merged wr_sectors wr_ticks ...
				unsigned long major, minor, rd_ios, wr_ios;
				char name[32];
				
				if (sscanf(start, "%lu %lu %31s %lu %*u %*u %*u %lu", 
				           &major, &minor, name, &rd_ios, &wr_ios) == 5) {
					// Only count whole disks (minor = 0), not partitions
					if (minor == 0) {
						total_reads += rd_ios;
						total_writes += wr_ios;
						disk_count++;
						
						if (debug_mode) {
							pr_info("block_led_trigger: Disk %s: reads=%lu writes=%lu\n", 
							        name, rd_ios, wr_ios);
						}
					}
				}
				
				start = end + 1;
			}
			
			current_total_ios = total_reads + total_writes;
		}
		filp_close(f, NULL);
	}
	
	// Read /proc/vmstat to check for dirty pages (buffered writes)
	f = filp_open("/proc/vmstat", O_RDONLY, 0);
	if (!IS_ERR(f)) {
		pos = 0;
		len = kernel_read(f, buf, sizeof(buf) - 1, &pos);
		if (len > 0) {
			buf[len] = '\0';
			
			// Look for "nr_dirty" line
			char *start = buf;
			char *end;
			
			while ((end = strchr(start, '\n')) != NULL) {
				*end = '\0';
				
				if (strncmp(start, "nr_dirty", 8) == 0) {
					sscanf(start, "nr_dirty %lu", &current_dirty_pages);
					if (debug_mode) {
						pr_info("block_led_trigger: Dirty pages: %lu\n", current_dirty_pages);
					}
					break;
				}
				
				start = end + 1;
			}
		}
		filp_close(f, NULL);
	}
	
	// Trigger LED if I/O counters increased OR if dirty pages increased (buffered writes)
	if ((current_total_ios > last_total_ios && last_total_ios != 0) ||
	    (current_dirty_pages > last_dirty_pages && last_dirty_pages != 0)) {
		unsigned long new_ios = current_total_ios - last_total_ios;
		unsigned long new_dirty = current_dirty_pages - last_dirty_pages;
		
		if (debug_mode) {
			if (current_total_ios > last_total_ios) {
				pr_info("block_led_trigger: Disk I/O detected! +%lu operations (reads=%lu, writes=%lu, disks=%d)\n", 
				        new_ios, total_reads, total_writes, disk_count);
			}
			if (current_dirty_pages > last_dirty_pages) {
				pr_info("block_led_trigger: Buffered writes detected! +%lu dirty pages\n", new_dirty);
			}
		}
		trigger_led_blink();
	}
	
	last_total_ios = current_total_ios;
	last_dirty_pages = current_dirty_pages;
	
	// Requeue the work to check again
	queue_delayed_work(activity_wq, &activity_work, msecs_to_jiffies(check_interval_ms));
}

static int __init block_led_trigger_init(void)
{
	pr_info("block_led_trigger: loading\n");

	// Register the LED trigger
	led_trigger_register_simple("block-activity", &ledtrig_block);
	
	// Initialize the blink timer
	timer_setup(&blink_timer, blink_timer_callback, 0);
	
	// Initialize the workqueue for activity monitoring
	activity_wq = create_singlethread_workqueue("block_led_activity");
	if (!activity_wq) {
		pr_err("block_led_trigger: failed to create workqueue\n");
		led_trigger_unregister_simple(ledtrig_block);
		return -ENOMEM;
	}
	
	INIT_DELAYED_WORK(&activity_work, check_disk_activity);
	queue_delayed_work(activity_wq, &activity_work, msecs_to_jiffies(check_interval_ms));

	pr_info("block_led_trigger loaded with disk activity monitoring!\n");
	pr_info("Use: echo block-activity > /sys/class/leds/YOUR_LED/trigger\n");
	pr_info("Debug: %s, Check interval: %lu ms, Blink delay: %lu ms\n", 
	        debug_mode ? "enabled" : "disabled", check_interval_ms, blink_delay_ms);
	return 0;
}

static void __exit block_led_trigger_exit(void)
{
	// Clean up workqueue
	if (activity_wq) {
		cancel_delayed_work_sync(&activity_work);
		destroy_workqueue(activity_wq);
	}
	
	// Clean up timer
	if (timer_pending(&blink_timer)) {
		timer_delete(&blink_timer);
	}
	
	led_trigger_unregister_simple(ledtrig_block);
	pr_info("block_led_trigger: unloaded\n");
}

module_init(block_led_trigger_init);
module_exit(block_led_trigger_exit);

MODULE_AUTHOR("your_mom");
MODULE_DESCRIPTION("LED trigger for block IO with activity monitoring");
MODULE_LICENSE("GPL");
