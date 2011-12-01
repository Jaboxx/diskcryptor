#ifndef _MISC_
#define _MISC_

#include "devhook.h"

NTSTATUS 
  io_device_control(
    dev_hook *hook, u32 ctl_code, void *in_data, u32 in_size, void *out_data, u32 out_size
	);

int io_fs_control(
	   HANDLE h_device, u32 ctl_code
	   );

HANDLE io_open_volume(wchar_t *dev_name);

NTSTATUS io_device_rw_block(
			dev_hook *hook, u32 func, void *buff, u32 size, u64 offset, u32 io_flags
			);

int io_verify_hook_device(dev_hook *hook);

int dc_device_rw(
	  dev_hook *hook, u32 function, void *buff, u32 size, u64 offset
	  );

void wait_object_infinity(void *wait_obj);

int start_system_thread(
		PKSTART_ROUTINE thread_start,
		PVOID           context,
		HANDLE         *handle
		);

int dc_set_security(
	  PDEVICE_OBJECT device
	  );

int dc_resolve_link(
	  wchar_t *sym_link, wchar_t *target, u16 length
	  );

int dc_get_mount_point(
      dev_hook *hook, wchar_t *buffer, u16 length
	  );

#ifdef DBG_COM
 void dc_com_dbg_init();
 void com_print(char *format, ...);
#endif

#endif