/* Win7Taskbar - native core - shared IDs of the Notification Area Icons
 * page (app.rc <-> TrayCplDialog.cpp)
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * The single place where the dialog template (resources/app.rc) and the
 * dialog procedure (src/TrayCplDialog.cpp) agree on resource numbers. The
 * .rc includes this header; the .cpp includes this header; nothing else
 * duplicates the numbers.
 */

#ifndef W7T_TRAY_CPL_RES_H
#define W7T_TRAY_CPL_RES_H

/* Dialog template (RT_DIALOG). */
#define IDD_TRAYCPL 130

/* Controls of page 1 - the icon list - and of the shared footer. */
#define IDC_TRAYCPL_TITLE     1001   /* big instruction line        */
#define IDC_TRAYCPL_DESC      1002   /* small description paragraph */
#define IDC_TRAYCPL_CHECK     1003   /* "Always show all icons..."  */
#define IDC_TRAYCPL_LIST      1004   /* custom "list" window class  */
#define IDC_TRAYCPL_COMBO     1005   /* the one and only combo box,  *
                                      * parked over the row being     *
                                      * edited (Win7 did the combo     *
                                      * in-place too)                 */
#define IDC_TRAYCPL_LINKSYS   1006   /* "Turn system icons on or off" */
#define IDC_TRAYCPL_LINKREST  1007   /* "Restore default icon behaviors" */
#define IDC_TRAYCPL_BACK      1008   /* page 2 -> page 1 link         */
#define IDC_TRAYCPL_GRP1      1009   /* page-1 content container      */
#define IDC_TRAYCPL_GRP2      1010   /* page-2 content container      */
#define IDC_TRAYCPL_CHK_NET   1011
#define IDC_TRAYCPL_CHK_VOL   1012
#define IDC_TRAYCPL_CHK_BAT   1013
#define IDC_TRAYCPL_DESC2     1014

#endif /* W7T_TRAY_CPL_RES_H */
