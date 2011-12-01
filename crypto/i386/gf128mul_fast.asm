;
;   *
;   * DiskCryptor - open source partition encryption tool
;   * Copyright (c) 2007
;   * ntldr <ntldr@freed0m.org> PGP key ID - 0xC48251EB4F8E4E6E
;   *
;   This program is free software: you can redistribute it and/or modify
;   it under the terms of the GNU General Public License as published by
;   the Free Software Foundation, either version 3 of the License, or
;   (at your option) any later version.
;
;   This program is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;   GNU General Public License for more details.
;
;   You should have received a copy of the GNU General Public License
;   along with this program.  If not, see <http://www.gnu.org/licenses/>.
;

format MS COFF

section '.text' code readable executable  

public gf128mul64_table as '_gf128mul64_table@12'             

macro xor_block  
{
  xor	 ebx, [ebp + eax]
  xor	 ecx, [ebp + eax + 04h]
  xor	 edx, [ebp + eax + 08h]
  xor	 edi, [ebp + eax + 0Ch]
}

gf128mul64_table:
 push	 esi
 push	 edi
 push	 ebx
 push	 ebp
 mov	 esi, [esp+18h] ; esi - a
 mov	 ebp, [esp+1Ch] ; edi - ctx
 movzx	 eax, byte [esi]
 shl	 eax, 4
 mov	 ebx, [ebp + eax]
 mov	 ecx, [ebp + eax + 04h]
 mov	 edx, [ebp + eax + 08h]
 mov	 edi, [ebp + eax + 0Ch]
 movzx	 eax, byte [esi+1]
 add	 eax, 100h
 shl	 eax, 4
 xor_block
 movzx	 eax, byte [esi+2]
 add	 eax, 200h
 shl	 eax, 4
 xor_block
 movzx	 eax, byte [esi+3]
 add	 eax, 300h
 shl	 eax, 4
 xor_block
 movzx	 eax, byte [esi+4]
 add	 eax, 400h
 shl	 eax, 4
 xor_block
 movzx	 eax, byte [esi+5]
 add	 eax, 500h
 shl	 eax, 4
 xor_block
 movzx	 eax, byte [esi+6]
 add	 eax, 600h
 shl	 eax, 4
 xor_block
 movzx	 eax, byte [esi+7]
 add     eax, 700h
 shl	 eax, 4
 xor_block
 mov	 esi, [esp+14h] ; esi - p
 mov	 [esi+00h], ebx
 mov	 [esi+04h], ecx
 mov	 [esi+08h], edx
 mov	 [esi+0Ch], edi
 pop	 ebp
 pop	 ebx
 pop	 edi
 pop	 esi
 retn    0Ch

