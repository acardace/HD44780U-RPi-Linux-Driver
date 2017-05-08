#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>

/* Simple device driver
 * for the Hitachi HD44780U LDC */

#define MODULE_NAME "hitachi-lcd"

#define CMD_TIME_START (40) /* 40 usec */
#define CMD_TIME_END (50)

#define DISPLAY_ON 1
#define DISPLAY_OFF 0
#define CURSOR 1
#define NO_CURSOR 0
#define BLINK 1
#define NO_BLINK 0
#define LINES_2 1
#define LINES_1 0
#define LCD_MAX_CHARS 40

#define BUF_SIZE 32

static DEFINE_MUTEX(hitachi_lcd_lock);
struct hitachi_lcd {
    struct gpio_desc *data[4];
    struct gpio_desc *rs;
    struct gpio_desc *rw;
    struct gpio_desc *e;
    char *buf;
    unsigned short index;
};

static struct hitachi_lcd lcd;

/* module params */
static int modeset = 1;
static int max_chars = LCD_MAX_CHARS;
/* Pins use BCM Numbering */
static int pin_d4 = 5;
static int pin_d5 = 6;
static int pin_d6 = 12;
static int pin_d7 = 13;
static int pin_rs = 16;
static int pin_rw = 26;
static int pin_e = 25;
module_param(modeset, int, 0444);
module_param(max_chars, int, 0444);
module_param(pin_d4, int, 0444);
module_param(pin_d5, int, 0444);
module_param(pin_d6, int, 0444);
module_param(pin_d7, int, 0444);
module_param(pin_rs, int, 0444);
module_param(pin_rw, int, 0444);
module_param(pin_e, int, 0444);

static struct gpio_desc *init_gpio_out(unsigned int n)
{
    struct gpio_desc *gpio;
    gpio = gpio_to_desc(n);
    if (IS_ERR(gpio)) {
        pr_err("hitachi-lcd: failed gpio %d allocation\n", n);
        return NULL;
    }
    if (gpiod_direction_output(gpio, GPIOD_OUT_LOW)) {
        pr_err("hitachi-lcd: failed set gpio direction\n");
        gpiod_put(gpio);
        return NULL;
    }
    return gpio;
}

static void put_gpios(struct hitachi_lcd *lcd)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (lcd->data[i])
            gpiod_put(lcd->data[i]);
    }
    if (lcd->rs)
        gpiod_put(lcd->rs);
    if (lcd->rw)
        gpiod_put(lcd->rw);
    if (lcd->e)
        gpiod_put(lcd->e);
}

static void clear_data_gpios(struct hitachi_lcd *lcd)
{
    gpiod_set_value(lcd->data[0], 0);
    gpiod_set_value(lcd->data[1], 0);
    gpiod_set_value(lcd->data[2], 0);
    gpiod_set_value(lcd->data[3], 0);
}

static void clear_all_gpios(struct hitachi_lcd *lcd)
{
    clear_data_gpios(lcd);
    gpiod_set_value(lcd->rs, 0);
    gpiod_set_value(lcd->rw, 0);
    gpiod_set_value(lcd->e, 0);
}

static void send_command(struct hitachi_lcd *lcd)
{
    gpiod_set_value(lcd->e, 1);
    usleep_range(CMD_TIME_START, CMD_TIME_END);
    gpiod_set_value(lcd->e, 0);
}

static void set_display(struct hitachi_lcd *lcd, unsigned display,
                        unsigned cursor, unsigned blink)
{
    clear_all_gpios(lcd);
    send_command(lcd);
    gpiod_set_value(lcd->data[3], 1);
    gpiod_set_value(lcd->data[2], display);
    gpiod_set_value(lcd->data[1], cursor);
    gpiod_set_value(lcd->data[0], blink);
    send_command(lcd);
}

static void setmode_4bit(struct hitachi_lcd *lcd, unsigned line_no)
{
    clear_all_gpios(lcd);
    gpiod_set_value(lcd->data[1], 1);
    send_command(lcd);
    send_command(lcd);
    gpiod_set_value(lcd->data[3], line_no);
    send_command(lcd);
}

static void clear_display(struct hitachi_lcd *lcd)
{
    clear_all_gpios(lcd);
    send_command(lcd);
    gpiod_set_value(lcd->data[0], 1);
    send_command(lcd);
}

static void entry_mode_set(struct hitachi_lcd *lcd)
{
    clear_all_gpios(lcd);
    send_command(lcd);
    gpiod_set_value(lcd->data[1], 1);
    gpiod_set_value(lcd->data[2], 1);
    send_command(lcd);
}

static void lcd_putchar(struct hitachi_lcd *lcd, char c)
{
    int i;

    clear_all_gpios(lcd);
    /* 1st nibble */
    gpiod_set_value(lcd->rs, 1);
    for (i = 4; i < 8; i++) {
        gpiod_set_value(lcd->data[i - 4], (c & (1 << i)) >> i);
    }
    send_command(lcd);

    /* 2nd nibble */
    for (i = 0; i < 4; i++) {
        gpiod_set_value(lcd->data[i], (c & (1 << i)) >> i);
    }
    send_command(lcd);
}

static void lcd_puts(struct hitachi_lcd *lcd, char *s, size_t size)
{
    int i;
    for (i = 0; i < size; i++) {
        if (lcd->index > max_chars) {
            lcd->index = 0;
            clear_display(lcd);
            msleep(100);
        }
        if (s[i] == '\n')
            continue;
        lcd_putchar(lcd, s[i]);
        lcd->index++;
    }
}

ssize_t lcd_write(struct file *file, const char __user *from, size_t size,
                  loff_t *off)
{
    size_t read_size;

    if (size < 1)
        return size;
    mutex_lock(&hitachi_lcd_lock);
    read_size = simple_write_to_buffer(lcd.buf, size, off, from, BUF_SIZE);
    if (read_size > 0) {
        lcd_puts(&lcd, lcd.buf, read_size);
    }
    mutex_unlock(&hitachi_lcd_lock);
    return read_size;
}

static struct file_operations lcd_ops = {
    .write = lcd_write, .read = NULL,
};

static struct miscdevice lcd_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = MODULE_NAME,
    .fops = &lcd_ops,
    .mode = 0222,
};

static int __init hitachi_lcd_load(void)
{
    int i;

    /* lcd char buffer */
    lcd.buf = kmalloc(BUF_SIZE, GFP_KERNEL);
    if (!lcd.buf)
        return -ENOMEM;

    lcd.data[0] = init_gpio_out(pin_d4); /* D4 */
    lcd.data[1] = init_gpio_out(pin_d5); /* D5 */
    lcd.data[2] = init_gpio_out(pin_d6); /* D6 */
    lcd.data[3] = init_gpio_out(pin_d7); /* D7 */
    lcd.rs = init_gpio_out(pin_rs);      /* RS */
    lcd.rw = init_gpio_out(pin_rw);      /* RW */
    lcd.e = init_gpio_out(pin_e);        /* E */
    lcd.index = 0;                       /* next position in the lcd */

    /* check correct init of gpios */
    if (!lcd.rs || !lcd.rw || !lcd.e)
        goto cleanup;
    for (i = 0; i < 4; i++) {
        if (!lcd.data[i])
            goto cleanup;
    }
    /* init display */
    /* set modeset to 0 if the display
     * has not been HW reinit */
    if (modeset)
        setmode_4bit(&lcd, LINES_1);
    set_display(&lcd, DISPLAY_ON, CURSOR, BLINK);
    entry_mode_set(&lcd);
    clear_display(&lcd);
    misc_register(&lcd_device);
    return 0;

cleanup:
    kfree(lcd.buf);
    put_gpios(&lcd);
    misc_deregister(&lcd_device);
    return -EIO;
}

static void __exit hitachi_lcd_unload(void)
{
    kfree(lcd.buf);
    set_display(&lcd, DISPLAY_OFF, NO_CURSOR, NO_BLINK);
    clear_display(&lcd);
    put_gpios(&lcd);
    misc_deregister(&lcd_device);
}

module_init(hitachi_lcd_load);
module_exit(hitachi_lcd_unload);

MODULE_AUTHOR("Antonio Cardace <anto.cardace@gmail.com>");
MODULE_LICENSE("GPL v2");
