/* Jasmine Batten and Jose Toledo */
/* COP 4610, Fall 2018 */


#include <linux/module.h>

static int hello_init(void)
{
	printk("Hello world. This is Jasmine Batten and Jose Toledo\n");
	printk("Hello world 2. This is Jasmine Batten and Jose Toledo\n");
	printk("Hello world 3. This is Jasmine Batten and Jose Toledo\n");
	return 0;
}

static void hello_exit(void)
{
	printk("Goodbye world. This is Jasmine Batten and Jose Toledo\n");
	printk("Goodbye world 2. This is Jasmine Batten and Jose Toledo\n");
	printk("Goodbye world 3. This is Jasmine Batten and Jose Toledo\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_AUTHOR("Jasmine Batten and Jose Toledo");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hello World");




