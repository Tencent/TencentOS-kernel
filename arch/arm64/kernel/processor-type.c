/*
*	Process Type Helper for Arm
*
*	Jasperwang, Tencent inc.
*/
#include <linux/of.h>
#include <linux/string.h>
#include <linux/processor-type.h>
#include <linux/dmi.h>

/* check phytium via dtb */
unsigned int  is_phytium2000(void)
{
	return of_machine_is_compatible("phytium,ft-2000a")
		|| of_machine_is_compatible("phytium,ft-2000ahk")
		|| of_machine_is_compatible("phytium,ft-2000plus");
}
EXPORT_SYMBOL(is_phytium2000);

/* check phytium via dtb */
unsigned int  is_phytium1500(void)
{
	return of_machine_is_compatible("phytium,ft-1500a");
}
EXPORT_SYMBOL(is_phytium1500);

/* check huawei via dmi info
 * notice: only available after subsys initcall !
*/
unsigned int  is_huawei(void)
{
	return dmi_name_in_vendors("HUAWEI")
		|| dmi_name_in_vendors("Huawei");
}
EXPORT_SYMBOL(is_huawei);

unsigned int  is_virtual(void)
{
	return dmi_name_in_vendors("QEMU")
		|| dmi_match(DMI_PRODUCT_NAME, "KVM Virtual Machine");
}
EXPORT_SYMBOL(is_virtual);
