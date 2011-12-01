/*  *
    * DiskCryptor - open source partition encryption tool
	* Copyright (c) 2007-2009 
	* ntldr <ntldr@diskcryptor.net> PGP key ID - 0xC48251EB4F8E4E6E
    *

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <windows.h>
#include <stdio.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <psapi.h>
#include <mbstring.h>
#include <prsht.h>
#include <strsafe.h>
#include <ntddscsi.h>
#include <shlwapi.h>

#include "misc.h"
#include "resource.h"
#include "linklist.h"
#include "prccode.h"
#include "defines.h"
#include "mbrinst.h"
#include "..\boot\boot.h"
#include "drv_ioctl.h"
#include "main.h"
#include "winreg.h"
#include "autorun.h"
#include "hotkeys.h"
#include "rand.h"
#include "subs.h"
#include "crypto\pkcs5.h"
#include "crypto\crypto.h"
#include "keyfiles.h"
#include "ukeyfiles.h"
#include "disk_name.h"

#pragma warning(disable : 4995)

int _tmr_elapse[ ] = 
{ 
	1000,    // MAIN_TIMER
	100,     // PROC_TIMER
	3000,    // RAND_TIMER
	500,     // HIDE_TIMER
	500,     // SHRN_TIMER
	500      // POST_TIMER
};

////////////////
char __hide_on_load = FALSE;
int  __status;

dc_conf_data __config;

list_entry __volumes;
list_entry __action;
list_entry __drives;

CRITICAL_SECTION crit_sect;

ATOM dlg_class;

///////////
static
int _finish_formating(_dnode *node)
{
	int rlt = dc_done_format(node->mnt.info.device);
	if (rlt == ST_OK) {

		if (wcscmp(node->dlg.fs_name, L"RAW") != 0) {
			rlt = dc_format_fs(node->mnt.info.w32_device, node->dlg.fs_name);
		}
	}
	if (rlt != ST_OK) {
		__error_s(
			__dlg, L"Error formatting volume [%s]", rlt, node->mnt.info.status.mnt_point
			);
	}
	return rlt;

}


static 
int _bench_cmp(
		const bench_item *arg1, 
		const bench_item *arg2
	)
{
	if (arg1->speed > arg2->speed) {
		return -1;
	} else {
		return (arg1->speed < arg2->speed);
	}
}


int _benchmark(
		bench_item *bench	
	)
{
	dc_bench   info;
	crypt_info crypt;
	int        i, n = 0;			

	for (i = 0; i < CF_CIPHERS_NUM; i++)
	{
		crypt.cipher_id = i;
		
		if (dc_benchmark(&crypt, &info) != ST_OK) {
			break;
		}

		bench[n].alg   = dc_get_cipher_name(i);
		bench[n].speed = (double)info.data_size / 
			( (double)info.enc_time / (double)info.cpu_freq) / 1024 / 1024;
		n++;

	}
	qsort(bench, n, sizeof(bench[0]), _bench_cmp);
	return n;

}


static
void _set_device_item(
		HWND     hlist,
		int      lvcount,
		int      num,
		wchar_t *mnt_point,
		_dnode  *root,
		BOOL     fixed,
		BOOL     installed,
		BOOL     boot
	)
{
	LVITEM lvitem;

	int lvsub = 0;
	__int64 size;	

	wchar_t s_size[MAX_PATH];
	wchar_t s_hdd[MAX_PATH];

	lvitem.mask = LVIF_TEXT | LVIF_PARAM;
	lvitem.iItem = lvcount;
	lvitem.iSubItem = 0;			
			
	lvitem.lParam = (LPARAM)root;
	lvitem.pszText = STR_NULL;
	ListView_InsertItem(hlist, &lvitem);
			
	size = dc_dsk_get_size(num, 0);
	dc_format_byte_size(s_size, sizeof_w(s_size), size);

	_snwprintf(s_hdd, sizeof_w(s_hdd), L"HardDisk %d", num);		

	ListView_SetItemText(hlist, lvcount, lvsub++, fixed ? s_hdd : mnt_point);
	ListView_SetItemText(hlist, lvcount, lvsub++, _wcslwr(s_size));

	ListView_SetItemText(hlist, lvcount, lvsub++, installed ? L"installed" : L"none");
	ListView_SetItemText(hlist, lvcount, lvsub++, boot ? L"boot" : STR_NULL);

}


void _list_devices(
		HWND hlist,
		BOOL fixed,
		int  sel
	)
{
	list_entry *node, *sub;

	int k   = 0;
	int col = 0;

	int lvcount     = 0;
	int boot_disk_1 = -1;
	int boot_disk_2 = -1;

	ldr_config conf;
	_dnode *root = malloc(sizeof(_dnode));

	zeroauto( root, sizeof(_dnode) );
	root->is_root = TRUE;

	_init_list_headers(hlist, _boot_headers);
	ListView_DeleteAllItems(hlist);

	dc_get_boot_disk( &boot_disk_1, &boot_disk_2 );
	if ( !fixed )
	{
		for ( node = __drives.flink;
			  node != &__drives;
			  node = node->flink ) 
		{						
			_dnode *drv = contain_record(node, _dnode, list);

			for ( sub = drv->root.vols.flink;
						sub != &drv->root.vols;
						sub = sub->flink ) 
			{
				dc_status *st = &contain_record(sub, _dnode, list)->mnt.info.status;
				if ( _is_removable_media(drv->root.dsk_num) )
				{
					_set_device_item(
						hlist, lvcount++, drv->root.dsk_num, st->mnt_point, 
						drv->root.dsk_num == sel ? root : NULL, FALSE, 
						dc_get_mbr_config( drv->root.dsk_num, NULL, &conf ) == ST_OK, 
						drv->root.dsk_num == boot_disk_1
						);
				}
			}
		}
	} else 
	{
		for ( ; k < 100; k++ ) 
		{
			if (dc_dsk_get_size(k, 0)) 
			{
				if (! _is_removable_media(k) )
				{
					_set_device_item(
						hlist, lvcount++, k, NULL, k == sel ? root : NULL,
						TRUE, dc_get_mbr_config( k, NULL, &conf ) == ST_OK, k == boot_disk_1
						);
				}
			}
		}
	}
	ListView_SetBkColor(hlist, GetSysColor(COLOR_BTNFACE));
	ListView_SetTextBkColor(hlist, GetSysColor(COLOR_BTNFACE));
	ListView_SetExtendedListViewStyle(hlist, LVS_EX_FLATSB | LVS_EX_FULLROWSELECT);

	if (ListView_GetItemCount(hlist) == 0) 
	{
		_list_insert_item(hlist, 0, 0, L"Volumes not found", 0);
		EnableWindow(hlist, FALSE);
	}

}


BOOL _list_part_by_disk_id(
		HWND hwnd,
		int  disk_id
	)
{
	list_entry *node;
	list_entry *sub;

	wchar_t s_id[MAX_PATH];
	wchar_t s_size[MAX_PATH];

	int count = 0;
	int item = 0;

	_init_list_headers(hwnd, _part_by_id_headers);
	ListView_DeleteAllItems(hwnd);

	for ( node = __drives.flink;
				node != &__drives;
				node = node->flink ) 
	{
		list_entry *vols = 
		&contain_record(node, _dnode, list)->root.vols;

		for ( sub = vols->flink;
					sub != vols;
					sub = sub->flink ) 
		{
			dc_status *status = &contain_record(sub, _dnode, list)->mnt.info.status;
			if ((status->flags & F_ENABLED) && (status->disk_id)) 
			{							
				dc_format_byte_size(
					s_size, sizeof_w(s_size), status->dsk_size
					);

				_snwprintf(s_id, sizeof_w(s_id), L"%.08X", status->disk_id);

				_list_insert_item(hwnd, count, 0, 
					status->mnt_point, status->disk_id == disk_id ? LVIS_SELECTED : FALSE);

				_list_set_item(hwnd, count, 1, s_size);
				_list_set_item(hwnd, count, 2, s_id);

				if (status->disk_id == disk_id) item = count;
				count++;				

			}
		}
	}
	if (!count) _list_insert_item(hwnd, count, 0, L"Partitions not found", 0);
	
	ListView_SetBkColor(hwnd, GetSysColor(COLOR_BTNFACE));
	ListView_SetTextBkColor(hwnd, GetSysColor(COLOR_BTNFACE));

	ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FLATSB | LVS_EX_FULLROWSELECT);
	ListView_SetSelectionMark(hwnd, item);

	return count;

}


static void 
_add_drive_node(
		_dnode    *exist_node,
		drive_inf *new_drv,
		vol_inf   *vol, 
		int        disk_number
	)
{
	wchar_t drvname[MAX_PATH];

	wchar_t fs[MAX_PATH]    = { 0 };
	wchar_t label[MAX_PATH] = { 0 };

	wchar_t path[MAX_PATH];

	list_entry *node;
	BOOL root_exists = FALSE;

	_dnode *root;
	_dnode *mnt;

	mnt = exist_node;
	if ( mnt == NULL )
	{
		mnt = malloc( sizeof(_dnode) );
		zeroauto( mnt, sizeof(_dnode) );
	}
	mnt->exists = TRUE;
	autocpy( &mnt->mnt.info, vol, sizeof(vol_inf) );

	_snwprintf( path, sizeof_w(path), L"%s\\", vol->status.mnt_point );
	GetVolumeInformation( path, label, sizeof_w(label), 0, 0, 0, fs, sizeof_w(fs) );

	wcscpy( mnt->mnt.label, label );
	wcscpy( mnt->mnt.fs, fs );

	if (! exist_node )
	{
		dc_get_hw_name(
			disk_number, vol->status.flags & F_CDROM, drvname, sizeof_w(drvname)
			);

		if (! ( vol->status.flags & F_CDROM ) )
		{
			for ( node  = __drives.flink;
				  node != &__drives;
				  node  = node->flink ) 
			{
				root = contain_record(node, _dnode, list);
				if ( root->root.dsk_num == disk_number )
				{
					root_exists = TRUE;
					break;
				}
			}
		}
		mnt->is_root = FALSE;
		autocpy( &mnt->root.info, new_drv, sizeof(drive_inf) );

		if (! root_exists )
		{
			root = malloc(sizeof(_dnode));	
			root->is_root = TRUE;

			autocpy(&root->mnt.info, vol, sizeof(vol_inf));
			autocpy(&root->root.info, new_drv, sizeof(drive_inf));

			wcscpy(root->root.dsk_name, drvname);
			root->root.dsk_num = disk_number;	

			_init_list_head(&root->root.vols);
			_insert_tail_list(&__drives, &root->list);

		} 
		_insert_tail_list(&root->root.vols, &mnt->list);

	} 		
	if ( vol->status.flags & F_SYNC && _create_act_thread(mnt, -1, -1) == NULL )
	{
		_create_act_thread(mnt, ACT_ENCRYPT, ACT_PAUSED);
	}
}


static
_dnode *_scan_vols_tree(
		vol_inf *vol,
		int     *count
	)
{
	list_entry *del;
	list_entry *node;
	list_entry *sub;	

	for ( node = __drives.flink;
		  node != &__drives
		  ;
		) 
	{
		_dnode *root = contain_record(node, _dnode, list);
		if (count) *count += 1;

		for ( sub = root->root.vols.flink;
			  sub != &root->root.vols
			  ; 
			) 
		{
			_dnode *mnt = contain_record(sub, _dnode, list);
			if ( count ) *count += 1;
				
			if (! vol )
			{
				if (! mnt->exists )
				{
					del = sub;
					sub = sub->flink;

					_remove_entry_list(del);
					free(del);

					continue;
				}
			} else {
				if ( ( wcscmp(mnt->mnt.info.device, vol->device) == 0 ) && (! mnt->exists) ) {
					return mnt;
				}
			}
			sub = sub->flink;
		}
		if (_is_list_empty(sub)) 
		{
			del = node;
			node = node->flink;

			_remove_entry_list(del);
			free(del);

			continue;
		}
		node = node->flink;

	}
	return NULL;

}


int _list_volumes(
		list_entry *volumes
	)
{
	DWORD drives = 0;

	u32 k     = 2;
	int count = 0;

	vol_inf   volinfo;
	drive_inf drvinfo;

	if ( dc_first_volume( &volinfo ) == ST_OK )
	{
		do 
		{
			_dnode *mnt = _scan_vols_tree( &volinfo, NULL );
			if (! mnt )
			{
				if ( volinfo.status.flags & F_CDROM )
				{
					_add_drive_node( NULL, &drvinfo, &volinfo, 0 );
					continue;
				}
				if ( dc_get_drive_info( volinfo.w32_device, &drvinfo ) != ST_OK ) continue;
				for ( k = 0; k < drvinfo.dsk_num; k++ ) 
				{
					_add_drive_node( NULL, &drvinfo, &volinfo, drvinfo.disks[k].number );
				}
			} else {
				do {
					_add_drive_node( mnt, NULL, &volinfo, 0 );
				} while ( (mnt = _scan_vols_tree(&volinfo, NULL)) != NULL );
			}

		} while (dc_next_volume(&volinfo) == ST_OK);
	}
	_scan_vols_tree(NULL, &count);
	return count;

}


BOOL _is_warning_item( LPARAM lparam ) 
{
	_dnode *info = pv(lparam);

	if (info && (info->mnt.info.status.flags & F_FORMATTING))
	return TRUE;	
	return FALSE;

}


BOOL _is_active_item( LPARAM lparam ) 
{
	_dnode *info = pv(lparam);

	if (info &&
		!info->is_root && 
		info->mnt.info.status.flags & F_UNSUPRT
		)
	return FALSE;
	return TRUE;

}


BOOL _is_root_item( LPARAM lparam ) 
{
	_dnode *info = pv(lparam);
	return info ? info->is_root : FALSE;
}

BOOL _is_enabled_item( LPARAM lparam ) 
{
	_dnode *info = pv(lparam);
	return info ? info->mnt.info.status.flags & F_ENABLED : FALSE;
}

BOOL _is_marked_item( LPARAM lparam ) 
{
	_dnode *info = pv(lparam);
	return info ? info->is_root && (info->root.dsk_name[0] == '\0') : FALSE;
}

BOOL _is_splited_item( LPARAM lparam )
{
	_dnode *info = pv(lparam);
	return info ? info->root.info.dsk_num > 1 : FALSE;
}

BOOL _is_cdrom_item( LPARAM lparam )
{
	_dnode *info = pv(lparam);
	return info ? info->mnt.info.status.flags & F_CDROM : FALSE;
}


BOOL _is_curr_in_group( HWND h_tab ) 
{
	_tab_data *tab;

	tab = wnd_get_long(GetParent(h_tab), GWL_USERDATA);
	return tab && (tab->curr_tab == TabCtrl_GetCurSel(h_tab));

}


BOOL _is_icon_show( 
		HWND   h_list,
		int    idx
	)
{
	WINDOWINFO winfo = { 0 };
	wchar_t    s_header[200] = { STR_HEAD_NO_ICONS };

	winfo.cbSize = sizeof( winfo );
	GetWindowInfo( h_list, &winfo );

	if (idx != -1)
	{
		_get_header_text( 
			h_list, idx, s_header, sizeof_w(s_header) 
			);
	}
	/*{
		LVCOLUMN lvc;

		lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_IMAGE;
		ListView_GetColumn( h_list, idx, &lvc );

		return lvc.fmt & LVCFMT_BITMAP_ON_RIGHT;
	}*/
	/*
	return ! (
		( winfo.dwStyle & LVS_NOCOLUMNHEADER ) && 
		( wcscmp( s_header, STR_HEAD_NO_ICONS ) == 0 )
	);
	*/
	//if (wcslen(s_header) < 3) __msg_i(__dlg, s_header);

	return (
		s_header[wcslen(s_header) - 1] == L' '
		);

	
	

}


BOOL _is_boot_device( vol_inf *vol )
{
	wchar_t boot_dev[MAX_PATH];

	dc_get_boot_device(boot_dev);
	return ( vol->status.flags & F_SYSTEM ) || (! wcscmp(vol->device, boot_dev) );

}


BOOL _is_removable_media( int dsk_num )
{
	dc_disk_p *d_info;
	BOOL       rem_media = FALSE;

	d_info = dc_disk_open(dsk_num, FALSE);
	if (d_info != NULL)
	{
		rem_media = d_info->media == RemovableMedia;
		dc_disk_close(d_info);
	} else {
		__error_s( __dlg, L"Error get volume information", ST_ACCESS_DENIED );
	}
	return rem_media;

}


void _load_diskdrives(
		HWND        hwnd,
		list_entry *volumes,
		char        vcount
	)
{ 
	LVITEM lvitem;

	list_entry *node;
	list_entry *sub;

	BOOL boot_enc = TRUE;
	BOOL run_enc  = TRUE;
	BOOL vol_enb  = FALSE;

	int count;

	wchar_t s_display[MAX_PATH] = { L"{ ERR_NAME }" };
	wchar_t s_boot_dev[MAX_PATH];

	int k       = 0;
	int col     = 0;
	int item    = 0;
	int subitem = 1;
		
	HWND hlist = GetDlgItem(hwnd, IDC_DISKDRIVES);

	SendMessage(hlist, WM_SETREDRAW, FALSE, 0);
	count = ListView_GetItemCount(hlist);

	_init_list_headers(hlist, _main_headers);
	if ( count != vcount )
	{
		ListView_DeleteAllItems(hlist);
		count = 0;
	}
	lvitem.mask      = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE | LVIF_PARAM; 
	lvitem.state     = 0; 
	lvitem.stateMask = 0;

	for ( node  = __drives.flink;
		  node != &__drives;
		  node  = node->flink, subitem = 1 
		  )
	{
		_dnode *root = contain_record(node, _dnode, list);

		lvitem.iItem    = item;
		lvitem.iSubItem = 0;
		lvitem.lParam   = (LPARAM)root;

		if (! count )
		{
			lvitem.iImage  = 0;
			lvitem.pszText = root->root.dsk_name;
			ListView_InsertItem(hlist, &lvitem);
		} else {
			lvitem.mask = LVIF_PARAM; 
			ListView_SetItem(hlist, &lvitem);
		}

		for ( sub  = root->root.vols.flink, item++;
			  sub != &root->root.vols;
			  sub  = sub->flink, item++, subitem = 1 
			  )
		{
			_dnode *mnt = contain_record(sub, _dnode, list);
			mnt->exists = FALSE;

			lvitem.iItem = item;
			lvitem.lParam = (LPARAM)mnt;
			lvitem.iSubItem = 0;

			if ( wcsstr( mnt->mnt.info.status.mnt_point, L"\\\\?\\" ) == 0 )
			{
				_snwprintf(
					s_display, sizeof_w(s_display), L"&%s", mnt->mnt.info.status.mnt_point
					);
			} else {
				wchar_t *vol_name = wcsrchr( mnt->mnt.info.device, L'V' );
				if (! vol_name ) vol_name = wcsrchr( mnt->mnt.info.device, L'\\' ) + 1;

				if ( (int)vol_name > 1 )
				{
					_snwprintf( s_display, sizeof_w(s_display), L"&%s", vol_name );
				}
			}
			if (! count )
			{
				lvitem.iImage  = 1; 
				lvitem.pszText = s_display;
				ListView_InsertItem( hlist, &lvitem );
			} else {
				lvitem.mask = LVIF_PARAM;
				ListView_SetItem( hlist, &lvitem );
			}
			_list_set_item_text( hlist, item, 0, s_display );

			dc_format_byte_size( s_display, sizeof_w(s_display), mnt->mnt.info.status.dsk_size );
			_list_set_item_text( hlist, item, subitem++, _wcslwr(s_display) );

			_list_set_item_text( hlist, item, subitem++, mnt->mnt.label );
			_list_set_item_text( hlist, item, subitem++, mnt->mnt.fs );

			_get_status_text( mnt, s_display, sizeof_w(s_display) );
			_list_set_item_text( hlist, item, subitem++, s_display );

			if ( mnt->mnt.info.status.flags & F_SYNC ) run_enc = FALSE;
			if ( mnt->mnt.info.status.flags & F_ENABLED && !_is_boot_device(&mnt->mnt.info) ) vol_enb = TRUE;

			if ( dc_get_boot_device(s_boot_dev) == ST_OK )
			{
				wchar_t s_boot[MAX_PATH] = { 0 };

				if ( wcscmp(mnt->mnt.info.device, s_boot_dev) == 0 ) wcscat( s_boot, L"boot" );
				if ( mnt->mnt.info.status.flags & F_SYSTEM )
				{
					if (wcslen(s_boot)) wcscat(s_boot, L", ");
					wcscat(s_boot, L"sys");
				}			
				if ( wcslen(s_boot) && mnt->mnt.info.status.flags & F_ENABLED ) boot_enc = FALSE; 
				_list_set_item_text( hlist, item, subitem++, s_boot );

			}
		}
	}	
	EnableMenuItem( GetMenu(__dlg), ID_TOOLS_DRIVER, _menu_onoff(boot_enc) );
	EnableMenuItem( GetMenu(__dlg), ID_TOOLS_BSOD, _menu_onoff(run_enc) );
	EnableWindow( GetDlgItem(hwnd, IDC_BTN_UNMOUNTALL_), vol_enb );

	SendMessage(hlist, WM_SETREDRAW, TRUE, 0);

} 


void _set_timer(
		int  index,
		BOOL set,
		BOOL refresh
	)
{
	if (refresh) _refresh(TRUE);
	if (set) 
	{
		SetTimer(__dlg, IDC_TIMER + index, 
			_tmr_elapse[index], (TIMERPROC)_timer_handle);

	} else {
		KillTimer(__dlg, IDC_TIMER + index);

	}
}


void _refresh(
		char main
	)
{
	_timer_handle(
		__dlg, WM_TIMER, IDC_TIMER + (main ? MAIN_TIMER : PROC_TIMER), IDC_TIMER
		);
}


void _state_menu(
		HMENU menu,
		UINT state
	)
{
	int count = GetMenuItemCount(menu);
	char k = 0;

	for ( ;k < count; k++ ) {
		EnableMenuItem(menu, GetMenuItemID(menu, k), state);

	}
}


void _refresh_menu( )
{
	HMENU menu = GetMenu(__dlg);
	HWND hlist = GetDlgItem(__dlg, IDC_DISKDRIVES);

	_dnode *node = pv(_get_sel_item(hlist));
	_dact *act = _create_act_thread(node, -1, -1);

	BOOL unmount = FALSE, mount   = FALSE;
	BOOL decrypt = FALSE, encrypt = FALSE;
	BOOL backup  = FALSE, restore = FALSE;

	BOOL format = FALSE, reencrypt = FALSE;
	BOOL del_mntpoint = FALSE, ch_pass = FALSE;

	if ( node && ListView_GetSelectedCount(hlist) && 
		 !_is_root_item((LPARAM)node) &&
	 	  _is_active_item((LPARAM)node)
		 )
	{
		int flags = node->mnt.info.status.flags;
	
		del_mntpoint = 
			wcsstr(node->mnt.info.status.mnt_point, L"\\\\?\\") == 0 && 
			IS_UNMOUNTABLE(&node->mnt.info.status);

		if ( flags & F_CDROM )
		{
			if ( flags & F_ENABLED )
			{
				unmount = TRUE;
			} else {
				if ( *node->mnt.fs == '\0' )
				{
					mount = TRUE;
				}
			}
		} else {
			backup = !( flags & F_SYNC );
	
			if ( flags & F_ENABLED )
			{
				if (flags & F_FORMATTING) 
				{
					format = TRUE;
				} else 
				{
					if ( IS_UNMOUNTABLE(&node->mnt.info.status) ) unmount = TRUE;
					if (! (act && act->status == ACT_RUNNING) )
					{
						if (! (flags & F_REENCRYPT) ) decrypt = TRUE;
						if (! (flags & F_SYNC) ) ch_pass = TRUE;

						if (flags & F_SYNC)	
						{
							encrypt = TRUE;
						} else {
							reencrypt = TRUE;
						}
					}
				}
			} else {
				restore = TRUE;
				if ( IS_UNMOUNTABLE(&node->mnt.info.status) ) format = TRUE;
	
				if ( *node->mnt.fs == '\0' )
				{
					mount = TRUE;
				}	else {
					encrypt = TRUE;
				}
			}
		}
	}
	SetWindowText(GetDlgItem(__dlg, IDC_BTN_MOUNT_), unmount ? IDS_UNMOUNT : IDS_MOUNT);
	EnableWindow(GetDlgItem(__dlg, IDC_BTN_MOUNT_), unmount || mount);

	EnableWindow(GetDlgItem(__dlg, IDC_BTN_ENCRYPT_), encrypt);
	EnableWindow(GetDlgItem(__dlg, IDC_BTN_DECRYPT_), decrypt);

	EnableMenuItem(menu, ID_VOLUMES_MOUNT, _menu_onoff(mount));
	EnableMenuItem(menu, ID_VOLUMES_ENCRYPT, _menu_onoff(encrypt));

	EnableMenuItem(menu, ID_VOLUMES_DISMOUNT, _menu_onoff(unmount));
	EnableMenuItem(menu, ID_VOLUMES_DECRYPT, _menu_onoff(decrypt));

	EnableMenuItem(menu, ID_VOLUMES_BACKUPHEADER, _menu_onoff(backup));
	EnableMenuItem(menu, ID_VOLUMES_RESTOREHEADER, _menu_onoff(restore));

	EnableMenuItem(menu, ID_VOLUMES_CHANGEPASS, _menu_onoff(ch_pass));	
	EnableMenuItem(menu, ID_VOLUMES_DELETE_MNTPOINT, _menu_onoff(del_mntpoint));	

	EnableMenuItem(menu, ID_VOLUMES_FORMAT, _menu_onoff(format));
	EnableMenuItem(menu, ID_VOLUMES_REENCRYPT, _menu_onoff(reencrypt));

}


int _menu_update_loader(
		HWND     hwnd,
		wchar_t *vol,
		int      dsk_num
	)
{
	int rlt = ST_ERROR;

	if ((rlt = dc_update_boot(dsk_num)) == ST_OK) 
	{
		__msg_i( hwnd, L"Bootloader on [%s] successfully updated\n", vol );
	} else {
		__error_s( hwnd, L"Error updated bootloader\n", rlt );
	}
	return rlt;

}


int _menu_unset_loader_mbr(
		HWND     hwnd,
		wchar_t *vol,
		int      dsk_num,
		int      type
	)
{
	int rlt = ST_ERROR;
	if (type == CTL_LDR_STICK) 
	{
		wchar_t dev[MAX_PATH];
		drive_inf inf;

		_snwprintf(dev, sizeof_w(dev), L"\\\\.\\%s", vol);
		rlt = dc_get_drive_info(dev, &inf);

		if (rlt == ST_OK) 
		{
			if (inf.dsk_num == 1) 
			{
				dsk_num = inf.disks[0].number;
			} else {
				__msg_w( hwnd, L"One volume on two disks\nIt's very strange.." );
				return rlt;
			}								
		}
	}
	if (__msg_q(
			hwnd, 
			L"Are you sure you want to remove bootloader\n"
			L"from [%s]?", vol)
			)
	{
		rlt = dc_unset_mbr(dsk_num);
		if (rlt == ST_OK) 
		{
			__msg_i( hwnd, L"Bootloader successfully removed from [%s]\n", vol );
		} else {
			__error_s( hwnd, L"Error removing bootloader\n", rlt );
		}
		return rlt;
	} else {
		return ST_CANCEL;
	}

}


int _menu_set_loader_vol(
		HWND     hwnd,
		wchar_t *vol,
		int      dsk_num,
		int      type
	)
{
	ldr_config conf;
	int rlt = ST_ERROR;

	if (type == CTL_LDR_STICK) 
	{
		if ((rlt = dc_set_boot(vol, FALSE)) == ST_FORMAT_NEEDED) 
		{
			if (__msg_q(
					hwnd,
					L"Removable media not correctly formatted\n"
					L"Format media?\n")
					)
			{
				rlt = dc_set_boot(vol, TRUE);
			}
		}

		if (rlt == ST_OK) 
		{
			if ((rlt = dc_mbr_config_by_partition(vol, FALSE, &conf)) == ST_OK ) 
			{				
				conf.options  |= OP_EXTERNAL;
				conf.boot_type = BT_AP_PASSWORD;

				rlt = dc_mbr_config_by_partition(vol, TRUE, &conf);
			}
		}
	} else {							
		rlt = _set_boot_loader( hwnd, dsk_num );
	}

	if (ST_OK == rlt) 
	{
		__msg_i( hwnd, L"Bootloader successfully installed to [%s]", vol );
	} else {
		__error_s( hwnd, L"Error install bootloader", rlt );
	}
	return rlt;

}


int _menu_set_loader_file(
		HWND     hwnd,
		wchar_t *path,
		BOOL     iso
	)
{
	ldr_config conf;

	int rlt = ST_ERROR;
	wchar_t *s_img = iso ? L"ISO" : L"PXE";

	rlt = iso ? dc_make_iso(path) : dc_make_pxe(path);
	if ( rlt == ST_OK )
	{
		if ( (rlt = dc_get_mbr_config( 0, path, &conf )) == ST_OK )
		{
			conf.options   |= OP_EXTERNAL;
			conf.boot_type  = BT_MBR_FIRST;

			rlt = dc_set_mbr_config(0, path, &conf);
		}			
	}
	if ( rlt == ST_OK )
	{
		__msg_i( hwnd, L"Bootloader %s image file \"%s\" successfully created", s_img, path );
	} else {
		__error_s( hwnd, L"Error creating %s image", rlt, s_img );
	}
	return rlt;

}


void _menu_decrypt(
		_dnode *node
	)
{
	dlgpass dlg_info = { node, NULL, NULL, NULL };

	int rlt;
	if (!_create_act_thread(node, -1, -1)) 
	{
		rlt = _dlg_get_pass(__dlg, &dlg_info);
		if (rlt == ST_OK) 
		{
			rlt = dc_start_decrypt(node->mnt.info.device, dlg_info.pass);
			secure_free(dlg_info.pass);

			if (rlt != ST_OK) 
			{
				__error_s(
					__dlg, L"Error start decrypt volume [%s]", rlt, node->mnt.info.status.mnt_point
				);
			}
		}
	} else rlt = ST_OK;
	if (rlt == ST_OK) 
	{
		_create_act_thread(node, ACT_DECRYPT, ACT_RUNNING);
		_activate_page( );

	}
}


int _set_boot_loader(
		HWND hwnd,
		int  dsk_num
	)
{
	int boot_disk_1 = dsk_num;
	ldr_config conf;

	int rlt;
	/*
	if (boot_disk_1 == -1)
	{
		rlt = dc_get_boot_disk( &boot_disk_1, &boot_disk_2 );
		if ( rlt != ST_OK ) return rlt;
	}
	*/
	if ( (rlt = dc_set_mbr( boot_disk_1, 0) ) == ST_NF_SPACE )
	{
		if (__msg_w( hwnd,
				L"Not enough space after partitions to install bootloader.\n\n"
				L"Install bootloader to first HDD track?\n"
				L"(incompatible with third-party bootmanagers, like GRUB)"
				)
			) 
		{
			if ((( rlt = dc_set_mbr( boot_disk_1, 1) ) == ST_OK ) && 
				 ( dc_get_mbr_config( boot_disk_1, NULL, &conf ) == ST_OK )
				) 
			{
				conf.boot_type = BT_ACTIVE;						
				if ( (rlt = dc_set_mbr_config( boot_disk_1, NULL, &conf )) != ST_OK )
				{
					dc_unset_mbr( boot_disk_1 );
				}
			}
		}
	}
	return rlt;

}


DWORD 
WINAPI 
_thread_format_proc(
		LPVOID lparam
	)
{
	int i = 0;
	int rlt, wp_mode;

	wchar_t device[MAX_PATH];
	_dnode *node;
	_dact  *act;

	dc_open_device( );
	EnterCriticalSection(&crit_sect);

	node = pv(lparam);
	act = _create_act_thread(node, -1, -1);

	if (!node || !act) return 0L;

	wcscpy(device, act->device);

	do {
		if (act->status != ACT_RUNNING) break;
		if (i-- == 0) 
		{
			dc_sync_enc_state(device); 
			i = 20;
		}
		wp_mode = act->wp_mode;
		LeaveCriticalSection(&crit_sect);

		rlt = dc_format_step(device, wp_mode);
		
		EnterCriticalSection(&crit_sect);
		if (rlt == ST_FINISHED) 
		{
			act->status = ACT_STOPPED;
			break;
		}
		if ((rlt != ST_OK) && (rlt != ST_RW_ERR))
		{
			dc_status st;
			dc_get_device_status( device, &st );

			__error_s(
				HWND_DESKTOP,
				L"Format error on volume [%s]", rlt, st.mnt_point
				);
			
			act->status = ACT_STOPPED;
			break;
		}
	} while (1);

	if (rlt != ST_FINISHED) 
	{
		_finish_formating(node);
	}
	LeaveCriticalSection(&crit_sect);
	dc_close_device( );

	return 1L;

}


static
DWORD WINAPI 
_thread_enc_dec_proc(
		LPVOID lparam
	)
{
	BOOL encrypting;
	int i = 0;
	int rlt, wp_mode;

	wchar_t device[MAX_PATH];
	_dnode *node;
	_dact *act;

	dc_open_device( );
	EnterCriticalSection(&crit_sect);

	node = pv(lparam);
	act = _create_act_thread(node, -1, -1);
	if (!node || !act) return 0L;

	wcscpy(device, act->device);
	do 
	{
		if (act->status != ACT_RUNNING) break;
		if (i-- == 0) 
		{
			dc_sync_enc_state(device); 
			i = 20;
		}
		encrypting = act->act != ACT_DECRYPT;
		wp_mode = act->wp_mode;

		LeaveCriticalSection(&crit_sect);

		rlt = encrypting ?
			dc_enc_step(device, wp_mode) :
			dc_dec_step(device);

		EnterCriticalSection(&crit_sect);
		if (rlt == ST_FINISHED) 
		{
			act->status = ACT_STOPPED;
			break;
		}
		if (rlt == ST_CANCEL)
		{
			Sleep(5000);
		}
		if ((rlt != ST_OK) && (rlt != ST_RW_ERR) && (rlt != ST_CANCEL))
		{
			dc_status st;
			wchar_t *act_name;

			dc_get_device_status( device, &st );
			switch ( act->act )
			{
				case ACT_ENCRYPT:   act_name = L"Encryption";   break;
				case ACT_DECRYPT:   act_name = L"Decryption";   break;
				case ACT_REENCRYPT: act_name = L"Reencryption"; break;
			}
			__error_s(
				HWND_DESKTOP,
				L"%s error on volume [%s]", rlt, act_name, st.mnt_point
				);
			
			act->status = ACT_STOPPED;
			break;
		}
	} while (1);

	dc_sync_enc_state(device);
	LeaveCriticalSection(&crit_sect);

	dc_close_device( );
	return 1L;

}


void _clear_act_list( )
{
	list_entry *node = __action.flink;
	list_entry *del = NULL;

	list_entry *head = &__action;

	for ( ;
		node != &__action;		
	)
	{
		_dact *act = contain_record(node, _dact, list);
		if (ACT_STOPPED == act->status) 
		{
			if (WaitForSingleObject(act->h_thread, 0) == WAIT_OBJECT_0) 
			{
				del = node;
				node = node->flink;

				_remove_entry_list(del); 

				CloseHandle(act->h_thread);
				free(del);
			
				continue;
			}
		}
		node = node->flink;
	}

}


_dact *_create_act_thread(
		_dnode *node,
		int     act_type,   // -1 - search
		int     act_status  //
	)
{
	list_entry *item;
	_dact      *act;

	DWORD resume;	
	BOOL  exist = FALSE;

	if (!node) return NULL;
	_clear_act_list( );

	for ( 
		item = __action.flink;
		item != &__action; 
		item = item->flink 
		) 
	{
		act = contain_record(item, _dact, list);
		if (!wcscmp(act->device, node->mnt.info.device)) 
		{
			exist = TRUE;
			if (act_type == -1) return act; else break;
		}
	}
	if (act_type != -1) 
	{
		if (!exist) 
		{
			act = malloc(sizeof(_dact));
			zeroauto(act, sizeof(_dact));
		
			act->wp_mode = node->mnt.info.status.crypt.wp_mode;
			wcsncpy( act->device, node->mnt.info.device, MAX_PATH );
			
			_init_speed_stat( &act->speed );
		}
		act->h_thread = NULL;
		act->status   = act_status;					
		act->act      = act_type;	

		if ( act_status == ACT_RUNNING )
		{
			void *proc;
			switch (act_type) 
			{
				case ACT_REENCRYPT:
				case ACT_ENCRYPT:
				case ACT_DECRYPT:   proc = _thread_enc_dec_proc; break;
				case ACT_FORMAT:    proc = _thread_format_proc;  break;				
			}
			act->h_thread = CreateThread(
				NULL, 0, proc, pv(node), CREATE_SUSPENDED, NULL
				);

			SetThreadPriority(act->h_thread, THREAD_PRIORITY_LOWEST);
			resume = ResumeThread(act->h_thread);

			if (!act->h_thread || resume == (DWORD)-1) 
			{
				free(act);
				
				__error_s( __dlg, L"Error create thread", -1 );
				return NULL;
			}
		}
		if (!exist) {
			_insert_tail_list(&__action, &act->list);

		}
		return act;			
	}
 	return NULL;

}


void _menu_encrypt_cd(  )
{
	_dnode *node = pv( malloc(sizeof(_dnode)) );		
	zeroauto( node, sizeof(_dnode) );
	
	wcscpy(node->mnt.info.device, L"Encrypt iso-file");
	node->dlg.act_type = ACT_ENCRYPT_CD;

	DialogBoxParam(
		__hinst, MAKEINTRESOURCE(IDD_WIZARD_ENCRYPT), __dlg, pv(_wizard_encrypt_dlg_proc), (LPARAM)node
		);

	if ( node->dlg.rlt == ST_CANCEL ) return;
	if ( node->dlg.rlt == ST_OK )
	{
		__msg_i( 
			__dlg, L"Iso-image \"%s\" successfully encrypted to \"%s\"", 
			_extract_name(node->dlg.iso.s_iso_src), 
			_extract_name(node->dlg.iso.s_iso_dst)
			);		
	} else {
		__error_s(
			__dlg, 
			L"Error encrypt iso-image \"%s\"", node->dlg.rlt, _extract_name(node->dlg.iso.s_iso_src) 
			);
	}
	free(node);
}


void _menu_encrypt(_dnode *node)
{
	int rlt;

	if (_create_act_thread(node, -1, -1) == 0)
	{
		node->dlg.act_type = ACT_ENCRYPT;

		DialogBoxParam(
			__hinst, MAKEINTRESOURCE(IDD_WIZARD_ENCRYPT), __dlg, pv(_wizard_encrypt_dlg_proc), (LPARAM)node
			);

		rlt = node->dlg.rlt;

	} else {
		rlt = ST_OK;
	}
	
	if (rlt == ST_CANCEL) return;
	if (rlt != ST_OK) 
	{
		__error_s(
			__dlg, L"Error start encrypt volume [%s]", rlt, node->mnt.info.status.mnt_point
			);
	} else {
		_create_act_thread(node, ACT_ENCRYPT, ACT_RUNNING);		
		_activate_page( );

	}
}


void _menu_wizard(_dnode *node)
{
	wchar_t *s_act;
	int rlt;

	if (_create_act_thread(node, -1, -1) == 0)
	{
		node->dlg.act_type = -1;

		DialogBoxParam(__hinst, 
			MAKEINTRESOURCE(IDD_WIZARD_ENCRYPT), __dlg, pv(_wizard_encrypt_dlg_proc), (LPARAM)node);

		rlt = node->dlg.rlt;

	} else {
		rlt = ST_OK;
	}
	
	if (rlt == ST_CANCEL) return;
	if (rlt != ST_OK) 
	{
		switch (node->dlg.act_type) 
		{
			case ACT_REENCRYPT: s_act = L"reencrypt"; break;
			case ACT_ENCRYPT:   s_act = L"encrypt";   break;
			case ACT_FORMAT:    s_act = L"format";    break;
		};
		__error_s(
			__dlg, L"Error start %s volume [%s]", rlt, s_act, node->mnt.info.status.mnt_point
			);
	} else {
		_create_act_thread(node, node->dlg.act_type, ACT_RUNNING);
		_activate_page( );

	}
}


void _menu_reencrypt(_dnode *node)
{
	int rlt;
	node->dlg.act_type = ACT_REENCRYPT;

	if (_create_act_thread(node, -1, -1) == 0 && 
		!(node->mnt.info.status.flags & F_REENCRYPT))
	{
		DialogBoxParam(__hinst, 
			MAKEINTRESOURCE(IDD_WIZARD_ENCRYPT), __dlg, pv(_wizard_encrypt_dlg_proc), (LPARAM)node);

		rlt = node->dlg.rlt;

	} else {
		rlt = ST_OK;
	}
	
	if (rlt == ST_CANCEL) return;

	if (rlt != ST_OK) 
	{
		__error_s(
			__dlg, L"Error start reencrypt volume [%s]", rlt, node->mnt.info.status.mnt_point
			);
	} else {
		_create_act_thread(node, ACT_REENCRYPT, ACT_RUNNING);
		_activate_page( );

	}
}


void _menu_format(_dnode *node)
{
	int rlt;

	node->dlg.act_type = ACT_FORMAT;
	node->dlg.q_format = FALSE;

	node->dlg.fs_name  = L"FAT32";

	if (_create_act_thread(node, -1, -1) == 0 && 
			!(node->mnt.info.status.flags & F_FORMATTING)
		)
	{
		DialogBoxParam(__hinst, 
			MAKEINTRESOURCE(IDD_WIZARD_ENCRYPT), __dlg, pv(_wizard_encrypt_dlg_proc), (LPARAM)node);

		rlt = node->dlg.rlt;

	} else {
		rlt = ST_OK;
	}
	
	if (rlt == ST_CANCEL) return;
	if (rlt != ST_OK) 
	{
		__error_s(
			__dlg, L"Error start format volume [%s]", rlt, node->mnt.info.status.mnt_point
			);
	} else 
	{
		if (node->dlg.q_format) _finish_formating(node);
		else
		{
			_create_act_thread(node, ACT_FORMAT, ACT_RUNNING);
			_activate_page( );

		}
	}
}


void _menu_unmount(_dnode *node)
{
	int resl  = ST_ERROR;
	int flags = __config.conf_flags & CONF_FORCE_DISMOUNT ? MF_FORCE : 0;

	if (__msg_q( __dlg, L"Unmount volume [%s]?", node->mnt.info.status.mnt_point) )
	{
		resl = dc_unmount_volume(node->mnt.info.device, flags);

		if (resl == ST_LOCK_ERR) 
		{
			if (__msg_w( __dlg,
					L"This volume contains opened files.\n"
					L"Would you like to force a unmount on this volume?" )) 
			{
				resl = dc_unmount_volume(node->mnt.info.device, MF_FORCE);
			} else {
				resl = ST_OK;
			}
		}

		if (resl != ST_OK) {
			__error_s(
				__dlg, L"Error unmount volume [%s]", resl, node->mnt.info.status.mnt_point
				);
		} else {
			_dact *act;

			EnterCriticalSection(&crit_sect);
			if (act = _create_act_thread(node, -1, -1)) 
			{
				act->status = ACT_STOPPED;
			}
			LeaveCriticalSection(&crit_sect);
		}
	}
}


void _menu_mount(_dnode *node)
{
	wchar_t mnt_point[MAX_PATH] = { 0 };
	wchar_t vol[MAX_PATH];

	dlgpass dlg_info = { node, NULL, NULL, mnt_point };

	int rlt;
	rlt = dc_mount_volume(node->mnt.info.device, NULL, (mnt_point[0] != 0) ? MF_DELMP : 0);

	if (rlt != ST_OK) 
	{
		if (_dlg_get_pass(__dlg, &dlg_info) == ST_OK) 
		{
			rlt = dc_mount_volume(node->mnt.info.device, dlg_info.pass, (mnt_point[0] != 0) ? MF_DELMP : 0);
			secure_free(dlg_info.pass);

			if (rlt == ST_OK)
			{						
				if (mnt_point[0] != 0)
				{
					_snwprintf(vol, sizeof_w(vol), L"%s\\", node->mnt.info.w32_device);
					_set_trailing_slash(mnt_point);

					if (SetVolumeMountPoint(mnt_point, vol) == 0) 
					{
						__error_s( __dlg, L"Error when adding mount point", rlt );
					}
				}
			} else 
			{
				__error_s(
					__dlg, L"Error mount volume [%s]", rlt, node->mnt.info.status.mnt_point
					);
			}
		}
	}
	if ( (rlt == ST_OK) && (__config.conf_flags & CONF_EXPLORER_MOUNT) ) {
		__execute(node->mnt.info.status.mnt_point);
	}
}


void _menu_mountall( )
{
	dlgpass dlg_info  = { NULL, NULL, NULL, NULL };
	int     mount_cnt = 0;	

	dc_mount_all(NULL, &mount_cnt, 0); 
	if (mount_cnt == 0) 
	{
		if (_dlg_get_pass(__dlg, &dlg_info) == ST_OK) 
		{
			dc_mount_all(dlg_info.pass, &mount_cnt, 0);
			secure_free(dlg_info.pass);

			__msg_i( __dlg, L"Mounted devices: %d", mount_cnt );

		}
	}
}


void _menu_unmountall( )
{
	list_entry *node = __action.flink;

	if ( __msg_q( __dlg, L"Unmount all volumes?" ) )
	{
		dc_unmount_all( );
		for ( ;node != &__action; node = node->flink ) 
		{
			((_dact *)node)->status = ACT_STOPPED;
		}
	}
}


void _menu_change_pass(_dnode *node)
{
	dlgpass dlg_info = { node, NULL, NULL, NULL };
	int     resl     = ST_ERROR;

	if (_dlg_change_pass(__dlg, &dlg_info) == ST_OK) 
	{
		resl = dc_change_password(
			node->mnt.info.device, dlg_info.pass, dlg_info.new_pass
			);

		secure_free(dlg_info.pass);
		secure_free(dlg_info.new_pass);

		if (resl != ST_OK) 
		{
			__error_s( __dlg, L"Error change password", resl );
		} else {
			__msg_i( __dlg, L"Password successfully changed for [%s]", node->mnt.info.status.mnt_point );
		}
	}
}


void _menu_clear_cache( )
{
	if ( __msg_q( __dlg, L"Wipe All Passwords?" ) )
	{
		dc_clean_pass_cache();
	}
}


void _menu_backup_header(_dnode *node)
{
	dlgpass dlg_info = { node, NULL, NULL, NULL };
	BYTE backup[DC_AREA_SIZE];

	wchar_t s_path[MAX_PATH];
	int rlt = _dlg_get_pass(__dlg, &dlg_info);

	if (rlt == ST_OK) 
	{
		rlt = dc_backup_header(node->mnt.info.device, dlg_info.pass, backup);
		secure_free(dlg_info.pass);

		if (rlt == ST_OK) 
		{
			_snwprintf(s_path, sizeof_w(s_path), L"%s.bin", wcsrchr(node->mnt.info.device, '\\')+1);
			if ( _save_file_dialog(__dlg, s_path, sizeof_w(s_path), L"Save backup volume header to file" ) ) 
			{
				rlt = save_file(s_path, backup, sizeof(backup));
			} else {
				return;
			}
		}
	} else return;

	if (rlt == ST_OK) 
	{
		__msg_i( __dlg, L"Volume header backup successfully saved to\n\"%s\"", s_path );
	} else {
		__error_s( __dlg, L"Error save volume header backup", rlt );

	}
}


void _menu_restore_header( _dnode *node )
{
	dlgpass dlg_info = { node, NULL, NULL, NULL };

	BYTE   backup[DC_AREA_SIZE];
	HANDLE hfile;

	wchar_t s_path[MAX_PATH] = { 0 };
	int     rlt = ST_ERROR;
	int     bytes;

	if (_open_file_dialog(__dlg, s_path, sizeof_w(s_path), L"Open backup volume header"))
	{		
		hfile = CreateFile(
			s_path, GENERIC_READ, 
			FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL
			);

		if (hfile != INVALID_HANDLE_VALUE) 
		{
			ReadFile(hfile, backup, sizeof(backup), &bytes, NULL);
			CloseHandle(hfile);

			if (_dlg_get_pass(__dlg, &dlg_info) == ST_OK) 
			{
				rlt = dc_restore_header(node->mnt.info.device, dlg_info.pass, backup);
				secure_free(dlg_info.pass);

			} else return;
		} else rlt = ST_NF_FILE;
	} else return;

	if (rlt == ST_OK) 
	{
		__msg_i( __dlg, L"Volume header successfully restored from\n\"%s\"", s_path );
	} else {
		__error_s( __dlg, L"Error restore volume header from backup", rlt );

	}
}


static int _dc_upd_bootloader( )
{
	ldr_config conf;
	
	if ( dc_get_mbr_config( -1, NULL, &conf ) != ST_OK )
	{
		return ST_OK; 
	} else {
		return dc_update_boot( -1 );
	}
}


int _drv_action(
		int action, 
		int version
	)
{
	int stat = dc_driver_status( );
	int rlt  = stat;
	static wchar_t restart_confirm[ ] = 
						L"You must restart your computer before the new settings will take effect.\n\n"
						L"Do you want to restart your computer now?";

	switch (action) 
	{
		case DA_INSTAL: 
		{
			if (stat == ST_INSTALLED) 
			{
				if ( __msg_q( HWND_DESKTOP, restart_confirm ) )
				{
					_reboot( );
				}
				rlt = ST_OK;
			}
			if (stat == ST_ERROR) 
			{
				if ( __msg_q( HWND_DESKTOP, L"Install DiskCryptor driver?" ) )
				{
					if ( (rlt = dc_install_driver(NULL)) == ST_OK )
					{
						if ( __msg_q( HWND_DESKTOP, restart_confirm ) )
						{
							_reboot( );					
						}						
					}
				} else {
					rlt = ST_OK;
				}
			}
		}
		break;
		case DA_REMOVE: 
		{
			if (stat != ST_ERROR) 
			{
				if ((rlt = dc_remove_driver(NULL)) == ST_OK)
				{
					if ( __msg_q( HWND_DESKTOP, restart_confirm ) )
					{
						_reboot( );
					}
				}
			}
		}
		break;
		case DA_UPDATE: 
		{
			wchar_t up_atom[MAX_PATH];

			_snwprintf(
				up_atom, sizeof_w(up_atom), L"DC_UPD_%d", version);

			if (GlobalFindAtom(up_atom) != 0)
			{
				if ( __msg_q( HWND_DESKTOP, restart_confirm ) ) _reboot( );
				rlt = rlt; break;
			}

			if (stat == ST_ERROR) break;
			if ( __msg_q( HWND_DESKTOP, L"Update DiskCryptor?" ) )
			{
				if (((rlt = dc_update_driver()) == ST_OK) &&
					  ((rlt = _dc_upd_bootloader()) == ST_OK))
				{
					if ( __msg_q( HWND_DESKTOP, restart_confirm ) ) {
						_reboot( );					
					}						
				}
			}
		}
		break;
	}
	return rlt;

}


static
LRESULT CALLBACK
_class_dlg_proc(
		HWND hwnd, 
		UINT message,
		WPARAM wparam, 
		LPARAM lparam
	)
{
	return DefDlgProc(hwnd, message, wparam, lparam);

}


int WINAPI wWinMain(
		HINSTANCE hinst,
		HINSTANCE hprev,
		LPWSTR    cmd_line,
		int       cmd_show
	)
{
	int rlt, ver;
	int app_start = on_app_start(cmd_line);

	if (app_start == ST_NEED_EXIT) 
	{
		return 0;
	}
	if (!_ui_init(hinst)) 
	{
		__error_s( HWND_DESKTOP, L"Error GUI initialization", ST_OK );
		return 0;
	}
	if (is_admin( ) != ST_OK) 
	{
		__error_s( HWND_DESKTOP, L"Admin Privileges Required", ST_OK );
		return 0;
	}
#ifdef _M_IX86 
	if (is_wow64( ) != 0) 
	{
		__error_s( HWND_DESKTOP, L"Please use x64 version of DiskCryptor", ST_OK );
		return 0;
	}
#endif
	if (dc_is_old_runned( ) != 0)
	{
		__error_s(
			HWND_DESKTOP, 
			L"DiskCryptor 0.1-0.4 installed, please completely uninstall it before use this version.", ST_OK
			);

		return 0;
	}
	if (dc_driver_status( ) != ST_OK)
	{
		if ((rlt = _drv_action(DA_INSTAL, 0)) != ST_OK) 
		{
			__error_s( HWND_DESKTOP, NULL, rlt );
		}
		return 0;
	}
	if ((rlt = dc_open_device( )) != ST_OK) 
	{
		__error_s( HWND_DESKTOP, L"Can not open DC device", rlt );
		return 0; 
	}
	
	ver = dc_get_version( );

	if (ver < DC_DRIVER_VER) 
	{
		if ((rlt = _drv_action(DA_UPDATE, ver)) != ST_OK) 
		{
			__error_s( HWND_DESKTOP, NULL, rlt );
		}
		return 0;
	}

	if (ver > DC_DRIVER_VER) 
	{
		__msg_i(
			HWND_DESKTOP,
			L"DiskCryptor driver v%d detected\n"
			L"Please use last program version", ver
			);

		return 0;
	}
	{
		HWND h_find;
		WNDCLASS wc = { 0 };

		wc.lpszClassName = DC_CLASS;
		wc.lpfnWndProc   = &_class_dlg_proc;
		wc.cbWndExtra    = DLGWINDOWEXTRA;
		wc.hIcon         = LoadIcon(hinst, MAKEINTRESOURCE(IDI_ICON_TRAY));

		dlg_class = RegisterClass(&wc);

		h_find = FindWindow(DC_CLASS, NULL);
		if (h_find != NULL)
		{
			ShowWindow(h_find, SW_SHOW);
			SetForegroundWindow(h_find);

			return 0;
		}
	}

	if ((rlt = rnd_init( )) != ST_OK)
	{
		__error_s( HWND_DESKTOP, L"Can not initialize RNG", rlt );
		return 0;
	}
	if ((rlt = dc_load_conf(&__config)) != ST_OK) 
	{
		__error_s( HWND_DESKTOP, L"Error get config", rlt );
		return 0;		
	}
	InitializeCriticalSection(&crit_sect);

	_init_list_head(&__drives);
	_init_list_head(&__action);

	_init_keyfiles_list( );

	return (int)
		DialogBoxParam(
				GetModuleHandleA(NULL), 
				MAKEINTRESOURCE(IDD_MAIN_DLG), 
				HWND_DESKTOP, 
				pv(_main_dialog_proc), 
				app_start == ST_AUTORUNNED
		);

}

