#include <sys/types.h>
#include <sys/sysctl.h>
#include <vm/vm_param.h>
#include <sys/vmmeter.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
	int hw_physmem[] = { CTL_HW, HW_PHYSMEM };
	int physmem;

	int hw_pagesize[] = { CTL_HW, HW_PAGESIZE };
	int pagesize;

	int vm_vmtotal[] = { CTL_VM, VM_METER };
	struct vmtotal vmdata;

	size_t len;
	int result;

	len = sizeof(physmem);
	result = sysctl(hw_physmem, sizeof(hw_physmem) / sizeof(*hw_physmem), &physmem, &len, NULL, 0);
	if (result != 0) return 1;

	len = sizeof(pagesize);
	result = sysctl(hw_pagesize, sizeof(hw_pagesize) / sizeof(*hw_pagesize), &pagesize, &len, NULL, 0);
	if (result != 0) return 1;

	len = sizeof(vmdata);
	result = sysctl(vm_vmtotal, sizeof(vm_vmtotal) / sizeof(*vm_vmtotal), &vmdata, &len, NULL, 0);

	// printf("Pagesize:%d\n", pagesize);
	printf("Total:%d\n", (physmem / (1024 * 1024)));
	printf("Free:%d\n", (pagesize / 1024)*(vmdata.t_free / 1024));
}

