#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include "font_8x8.h"           // lookup table to display 8x8 characters

/* procfs macros */
#define procfs_name "oled_driver"
#define PROCFS_MAX_SIZE 150

/* i2c macros */
#define I2C_BUS_AVAILABLE   (          1 )              // I2C Bus available in our Raspberry Pi
#define SLAVE_DEVICE_NAME   ( "OLED" )              // Device and Driver Name
#define SSD1315_SLAVE_ADDR  (       0x3C )              // SSD1315 OLED Slave Address
							
/* oled RAM structure macros */
#define PAGE_0 0xB0					// Address of Page 0 in GDDRAM
#define MAX_PAGE 0xB7					// Max no. of pages in GDDRAM
#define TOTAL_SEG 128					// Total segments in GDDRAM

/* ioctl commands */
#define DISPLAY_STRING _IOW('a', 'a', char*)
#define ZOOM_IN _IOW('a', 'b', int*)
#define BLINKING _IOW('a', 'c', int*)
#define SCROLLING _IOW('a', 'd', int*)
#define CLEAR_SCREEN _IOW('a', 'e', int*)

#define ON 1
#define NEWLINE 10
#define STRING_LIMIT 100

static struct i2c_adapter *i2c_adapter     = NULL;  // I2C Adapter Structure
static struct i2c_client  *i2c_client_oled = NULL;  // I2C Client Structure (In our case it is OLED)

dev_t dev = 0;
static struct class *dev_class;
static struct cdev oled_cdev;
struct kobject *kobj_ref;
static struct proc_dir_entry *pd_entry;

static long oled_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static void draw (char *display_string);
static void zoom_in (bool zoom_in);
static void fade_blink (bool blink);
static void scroll (bool blink);
static void clear_display (void);

/* Sysfs Functions */
static ssize_t string_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t string_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);

static ssize_t zoom_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t zoom_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);

static ssize_t blink_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t blink_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);

static ssize_t scroll_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t scroll_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);

/* attributes under the k_obj */
struct kobj_attribute display_attr = __ATTR(string_to_display, 0660, string_show, string_store);
struct kobj_attribute zoom_attr = __ATTR(zoom, 0660, zoom_show, zoom_store);
struct kobj_attribute blink_attr = __ATTR(blink, 0660, blink_show, blink_store);
struct kobj_attribute scroll_attr = __ATTR(scroll, 0660, scroll_show, scroll_store);

static struct attribute *oled_attrs [] = {
        &display_attr.attr,
        &zoom_attr.attr,
        &blink_attr.attr,
        &scroll_attr.attr,
        NULL
};

static struct attribute_group oled_display_group = {
        .name = "oled_attribute_group",
        .attrs = oled_attrs,
};

static struct file_operations fops =
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = oled_ioctl,
};


static long oled_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case DISPLAY_STRING:
			char user_string[STRING_LIMIT] = {'\0'};
			if (strncpy_from_user(user_string, (char*) arg, STRING_LIMIT)) {
				clear_display ();
                                printk (KERN_INFO "%s\n", user_string);
				draw (user_string);
			}
			else
				printk (KERN_INFO "Copy_from_user_failed\n");
			break;
		case ZOOM_IN:
			int zoom = 0;
			if (copy_from_user(&zoom, (int*) arg, sizeof(zoom)) == 0) {
				if (zoom == ON)
					zoom_in (true);
				else if (zoom > ON)
					printk (KERN_INFO "oled(zoom_in): value can be 0 or 1\n");

			}
			else
				printk (KERN_INFO "Copy_from_user_failed\n");
			break;
		case BLINKING:
			int blink = 0;
			if (copy_from_user(&blink, (int*) arg, sizeof(blink)) == 0) {
				if (blink == ON)
					fade_blink (true);
				else if (blink > ON)
					printk (KERN_INFO "oled(blinking): value can be 0 or 1\n");

			}
			else
				printk (KERN_INFO "Copy_from_user_failed\n");
			break;
		case SCROLLING:
			int scrolling = 0;
			if (copy_from_user(&scrolling, (int*) arg, sizeof(scrolling)) == 0) {
				if (scrolling == ON)
					scroll (true);
				else if (scrolling > ON)
					printk (KERN_INFO "oled(scroll): value can be 0 or 1\n");

			}
			else
				printk (KERN_INFO "Copy_from_user_failed\n");
			break;
	}
	return 0;
}

char string_to_display [STRING_LIMIT] = {'\0'};
int zoom_on = 0;
int blink_on = 0;
int scroll_on = 0;

static ssize_t string_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "oled:sysfs:string: Read!!!\n");
        return sprintf(buf, "%s\n", string_to_display);
}

static ssize_t string_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        printk(KERN_INFO "oled:sysfs:string: Write!!!\n");
        sscanf(buf,"%s",string_to_display);
        clear_display ();
        draw (string_to_display);
        return count;
}

static ssize_t zoom_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "oled:sysfs:zoom: Read!!!\n");
        return sprintf(buf, "%d\n", zoom_on);
}

static ssize_t zoom_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        printk(KERN_INFO "oled:sysfs:zoom: Write!!!\n");
        sscanf(buf,"%d",&zoom_on);
        printk ("%d\n", zoom_on);
        if (zoom_on == ON)
                zoom_in (true);
        else if (zoom_on == (!ON))
                zoom_in (false);
        else
                printk ("oled:sysfs:zoom:write: INVALID ARGUMENT (only 1/0 is valid)\n");
        return count;
}

static ssize_t blink_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "oled:sysfs:blink: Read!!!\n");
        return sprintf(buf, "%d\n", blink_on);
}

static ssize_t blink_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        printk(KERN_INFO "oled:sysfs:blink: Write!!!\n");
        sscanf(buf,"%d",&blink_on);
        if (blink_on == ON)
                fade_blink (true);
        else if (blink_on == (!ON))
                fade_blink (false);
        else
                printk ("oled:sysfs:blink:write: INVALID ARGUMENT (only 1/0 is valid)\n");
        return count;
}

static ssize_t scroll_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "oled:sysfs:scroll: Read!!!\n");
        return sprintf(buf, "%d\n", scroll_on);
}

static ssize_t scroll_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        printk(KERN_INFO "oled:sysfs:scroll: Write!!!\n");
        sscanf(buf,"%d", &scroll_on);
        if (scroll_on == ON)
                scroll (true);
        else if (scroll_on == (!ON))
                scroll (false);
        else
                printk ("oled:sysfs:scroll:write: INVALID ARGUMENT (only 1/0 is valid)\n");
        return count;
}

static ssize_t procfile_read(struct file* filp, char *buf, size_t count, loff_t *offp)
{
	int ret = 0;
	char tmp[PROCFS_MAX_SIZE] = {'\0'};
	printk(KERN_INFO "procfile_read (/proc/%s) called\n", procfs_name);

	if(*offp > 0)
	{
		return 0;
	}

	sprintf(tmp, "user string on display:%s\nzoom : %d\nblink: %d\nscroll: %d\n", string_to_display, zoom_on, blink_on, scroll_on);
	if(copy_to_user(buf, tmp, strlen(tmp)))
	{
		printk(KERN_ERR "Error in copy to user\n");
		return -EFAULT;
	}

	ret = *offp = strlen(tmp);
	return ret;
}

const static struct proc_ops proc_fops = /* for /proc operations */
{
	proc_read : procfile_read
};
/*
** This function writes the data into the I2C client
**
**  Arguments:
**      buff -> buffer to be sent
**      len  -> Length of the data
**   
*/
static int I2C_Write(unsigned char *buf, unsigned int len)
{
    /*
    ** Sending Start condition, Slave address with R/W bit, 
    ** ACK/NACK and Stop condtions will be handled internally.
    */ 
    int ret = i2c_master_send(i2c_client_oled, buf, len);
    
    return ret;
}

/*
** This function sends the command/data to the OLED.
**
**  Arguments:
**      is_cmd -> true = command, false = data
**      data   -> data to be written
** 
*/
static void SSD1315_Write(bool is_cmd, unsigned char data)
{
    unsigned char buf[2] = {0};
    int ret;
    
    /*
    ** First byte is always control byte. Data is followed after that.
    **
    ** There are two types of data in SSD_1306 OLED.
    ** 1. Command
    ** 2. Data
    **
    ** Control byte decides that the next byte is, command or data.
    **
    ** -------------------------------------------------------                        
    ** |              Control byte's | 6th bit  |   7th bit  |
    ** |-----------------------------|----------|------------|    
    ** |   Command                   |   0      |     0      |
    ** |-----------------------------|----------|------------|
    ** |   data                      |   1      |     0      |
    ** |-----------------------------|----------|------------|
    ** 
    ** Please refer the datasheet for more information. 
    **    
    */ 
    if (is_cmd == true){
        buf[0] = 0x00;
    }
    else {
        buf[0] = 0x40;
    }
    
    buf[1] = data;
    
    ret = I2C_Write(buf, 2);
}

/*
** This function sends the commands that need to used to Initialize the OLED.
**
**  Arguments:
**      none
** 
*/
static int SSD1315_DisplayInit(void)
{
    msleep(100);               // delay

    /*
    ** Commands to initialize the SSD_1315 OLED Display
    */
    SSD1315_Write(true, 0xAE); // Entire Display OFF
    SSD1315_Write(true, 0xD5); // Set Display Clock Divide Ratio and Oscillator Frequency
    SSD1315_Write(true, 0x80); // Default Setting for Display Clock Divide Ratio and Oscillator Frequency that is recommended
    SSD1315_Write(true, 0xA8); // Set Multiplex Ratio
    SSD1315_Write(true, 0x3F); // 64 COM lines
    SSD1315_Write(true, 0xD3); // Set display offset
    SSD1315_Write(true, 0x00); // 0 offset
    SSD1315_Write(true, 0x40); // Set first line as the start line of the display
    SSD1315_Write(true, 0x8D); // Charge pump
    SSD1315_Write(true, 0x14); // Enable charge dump during display on
    SSD1315_Write(true, 0x20); // Set memory addressing mode
    SSD1315_Write(true, 0x02); // Page addressing mode
    SSD1315_Write(true, 0xA1); // Set segment remap with column address 127 mapped to segment 0
    SSD1315_Write(true, 0xC8); // Set com output scan direction, scan from com 63 to com 0
    SSD1315_Write(true, 0xDA); // Set com pins hardware configuration
    SSD1315_Write(true, 0x12); // Alternative com pin configuration, disable com left/right remap
    SSD1315_Write(true, 0x81); // Set contrast control
    SSD1315_Write(true, 0x80); // Set Contrast to 128
    SSD1315_Write(true, 0xD9); // Set pre-charge period
    SSD1315_Write(true, 0xF1); // Phase 1 period of 15 DCLK, Phase 2 period of 1 DCLK
    SSD1315_Write(true, 0xDB); // Set Vcomh deselect level
    SSD1315_Write(true, 0x20); // Vcomh deselect level ~ 0.77 Vcc
    SSD1315_Write(true, 0xA4); // Entire display ON, resume to RAM content display
    SSD1315_Write(true, 0xA6); // Set Display in Normal Mode, 1 = ON, 0 = OFF
    SSD1315_Write(true, 0x2E); // Deactivate scroll
    SSD1315_Write(true, 0xAF); // Display ON in normal mode
    
    return 0;
}

static void clear_display (void)
{
	char page = PAGE_0;
	for (int i = 0; i < CHARS_COLS_LENGTH; i++) {

		SSD1315_Write(true, page++); // Setting Page address (page 0)
		SSD1315_Write(true, 0x00); // Setting Page address (higher nibble for col start address)
		SSD1315_Write(true, 0x10); // Setting Page address (lower nibble for col start address)
					   
		for(unsigned int j = 0; j < TOTAL_SEG; j++)
		{
			SSD1315_Write(false, 0x00);
		}
	}
	printk ("display cleared\n");
}
/*
** This function Fills the complete OLED with this data byte.
**
**  Arguments:
**      data  -> Data to be filled in the OLED
** 
*/
static void draw (char *data)
{
	SSD1315_Write(true, PAGE_0); // Setting Page address (page 0)
	SSD1315_Write(true, 0x00); // Setting Page address (higher nibble for col start address)
	SSD1315_Write(true, 0x10); // Setting Page address (lower nibble for col start address)

	unsigned int i = 0;
	unsigned char page = PAGE_0;
	unsigned int n_char = 0;
	unsigned int n = 1;

	while (data [i] != '\0') {
		n_char = (i + 1) * CHARS_COLS_LENGTH;

		if (((n * TOTAL_SEG) - n_char) == -CHARS_COLS_LENGTH) {
			page++;
			printk ("%c\n", page);
			if (page > MAX_PAGE) {
				printk (KERN_ALERT "oled@3c: Data exceeding 1kB\n");
				return;
			}
			SSD1315_Write(true, page); // Setting Page address (page 0)
			SSD1315_Write(true, 0x00); // Setting Page address (higher nibble for col start address)
			SSD1315_Write(true, 0x10); // Setting Page address (lower nibble for col start address)
			n++;
		}

		int ascii = data [i++];
		for (unsigned int j = 0; j < 8; j++)
		{
			if (ascii == NEWLINE) 
				SSD1315_Write(false, FONTS [0][j]);
			else 
				SSD1315_Write(false, FONTS [ascii - 32][j]);
		}
	}
}

static void scroll (bool scroll)
{
	if (!scroll) {
                SSD1315_Write(true, 0x2E);		//Deactivate Scroll
		return;
        }
	SSD1315_Write(true, 0x26);		//Configure Right Horizontal Scroll
	SSD1315_Write(true, 0x00);		//Set dummy byte
	SSD1315_Write(true, 0x00);		//Set start page address as page 0
	SSD1315_Write(true, 0x00);		//Set interval b/w scroll to 6 frame
	SSD1315_Write(true, 0x07);		//Set page end address as page 7
	SSD1315_Write(true, 0x00);		//dummy byte
	SSD1315_Write(true, 0xFF);		//dummy byte
	SSD1315_Write(true, 0x2F);		//Activate Scroll
}

static void fade_blink (bool blink)
{
	SSD1315_Write(true, 0x23);		//Configure fade and blink mode
	if (!blink) {
		SSD1315_Write(true, 0x00);		// Disable fade and blink mode
		return;
	}
	SSD1315_Write(true, 0x30);		// Enable fade and blink mode
}

static void zoom_in (bool zoom_in)
{
	SSD1315_Write(true, 0xD6);		//Configure zoom in mode
	if (!zoom_in) {
		SSD1315_Write(true, 0x00);		//disable zoom in
		return;
	}
	SSD1315_Write(true, 0x01);		//enable zoom in
}

/*
** This function getting called when the slave has been found
** Note : This will be called only once when we load the driver.
*/
static int oled_probe(struct i2c_client *client)
{
	SSD1315_DisplayInit();
	clear_display ();
	char *instruction = "Use test app or sysfs interface to display your string.";
	draw (instruction);
	pr_info("OLED Probed!!!\n");
	fade_blink (false);
	scroll (false);
	zoom_in (false);
	return 0;
}

/*
** This function getting called when the slave has been removed
** Note : This will be called only once when we unload the driver.
*/
static void oled_remove(struct i2c_client *client)
{   
    clear_display ();
    SSD1315_Write(true, 0x23);		//Configure fade and blink mode
    SSD1315_Write(true, 0x00);		//disable zoom in
    SSD1315_Write(true, 0x2E);		//Deactivate Scroll
    pr_info("OLED Removed!!!\n");
}

/*
** Structure that has slave device id
*/
static const struct i2c_device_id oled_id[] = {
        { SLAVE_DEVICE_NAME, 0 },
        { }
};
MODULE_DEVICE_TABLE(i2c, oled_id);

/*
** I2C driver Structure that has to be added to linux
*/
static struct i2c_driver oled_driver = {
        .driver = {
            .name   = SLAVE_DEVICE_NAME,
            .owner  = THIS_MODULE,
        },
        .probe          = oled_probe,
        .remove         = oled_remove,
        .id_table       = oled_id,
};

/*
** I2C Board Info strucutre
*/
static struct i2c_board_info oled_i2c_board_info = {
        I2C_BOARD_INFO(SLAVE_DEVICE_NAME, SSD1315_SLAVE_ADDR)
    };

/*
** Module Init function
*/
static int __init oled_driver_init(void)
{
	int ret = -1;
	i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
	if( i2c_adapter != NULL ) {
	i2c_client_oled = i2c_new_client_device(i2c_adapter, &oled_i2c_board_info);

	if( i2c_client_oled != NULL ) {
	    i2c_add_driver(&oled_driver);
	    ret = 0;
	}
	i2c_put_adapter(i2c_adapter);
	}
	/* Allocating Major number */
	if((alloc_chrdev_region(&dev, 0, 1, "oled_device")) <0) {
		printk(KERN_INFO "Cannot allocate major number\n");
		return -1;
	}
	printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
	/* Creating cdev structure */
	cdev_init(&oled_cdev, &fops);
	/* Adding character device to the system */
	if((cdev_add(&oled_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system\n");
		goto r_cdev;
	}
	/* Creating struct class */
	if((dev_class = class_create("oled_class")) == NULL) {
		printk(KERN_INFO "Cannot create the struct class\n");
		goto r_class;
	}
	/* Creating device */
	if((device_create(dev_class, NULL, dev, NULL, "oled_device")) == NULL) {
		printk(KERN_INFO "oled: Cannot create the Device \n");
		goto r_device;
	}

        /* Creating a directory in /sys/kernel/ */
        if(sysfs_create_group (kernel_kobj, &oled_display_group)) {
                printk(KERN_INFO"Cannot create sysfs file......\n");
                goto r_sysfs;
        }

	pd_entry = proc_create(procfs_name, 0, NULL, &proc_fops);

	if(pd_entry == NULL) {
		remove_proc_entry(procfs_name, NULL);
		printk(KERN_ERR "Could not initialize /proc/%s\n", procfs_name);
		return -ENOMEM;
	}

	printk(KERN_INFO "/proc/%s created\n", procfs_name);
	printk(KERN_INFO "Try using \"cat /proc/%s\"\n", procfs_name);
	ret = 0; 
	pr_info("Driver Added!!!\n");
	return ret;

r_sysfs:
kobject_put(kobj_ref);
sysfs_remove_group(kernel_kobj, &oled_display_group);
r_device:
class_destroy(dev_class);
r_class:
cdev_del(&oled_cdev);
r_cdev:
unregister_chrdev_region(dev,1);
return -1;

}

/*
** Module Exit function
*/
static void __exit oled_driver_exit(void)
{
	i2c_unregister_device(i2c_client_oled);
	i2c_del_driver(&oled_driver);
        kobject_put(kobj_ref);
        sysfs_remove_group(kernel_kobj, &oled_display_group);
	remove_proc_entry(procfs_name, NULL);
	device_destroy(dev_class,dev);
	class_destroy(dev_class);
	cdev_del(&oled_cdev);
	unregister_chrdev_region(dev, 1);
	pr_info("Driver Removed!!!\n");
}

module_init(oled_driver_init);
module_exit(oled_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Archita");
MODULE_DESCRIPTION("Oled display driver using I2C");
