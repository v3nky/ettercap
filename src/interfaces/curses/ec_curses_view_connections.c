/*
    ettercap -- curses GUI

    Copyright (C) ALoR & NaGA

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id: ec_curses_view_connections.c,v 1.8 2004/03/03 22:09:01 alor Exp $
*/

#include <ec.h>
#include <wdg.h>
#include <ec_curses.h>
#include <ec_conntrack.h>
#include <ec_manuf.h>
#include <ec_services.h>
#include <ec_strings.h>
#include <ec_format.h>
#include <ec_inject.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* proto */

void curses_show_connections(void);
static void curses_kill_connections(void);
static void refresh_connections(void);
static void curses_connection_detail(void *conn);
static void curses_connection_data(void *conn);
static void curses_connection_data_split(void);
static void curses_connection_data_join(void);
static void curses_destroy_conndata(void);
static void split_print(u_char *text, size_t len, struct ip_addr *L3_src);
static void split_print_po(struct packet_object *po);
static void join_print(u_char *text, size_t len, struct ip_addr *L3_src);
static void join_print_po(struct packet_object *po);
static void curses_connection_kill(void *conn);
static void curses_connection_kill_wrapper(void);
static void curses_connection_inject(void);
static void inject_user(void);
static void curses_connection_inject_file(void);
static void inject_file(char *path, char *file);

/* globals */

static wdg_t *wdg_connections, *wdg_conn_detail;
static wdg_t *wdg_conndata, *wdg_c1, *wdg_c2, *wdg_join;
static struct conn_object *curr_conn;
   
/* keep it global, so the memory region is always the same (reallocing it) */
static u_char *dispbuf;
static u_char *injectbuf;

/*******************************************/


/*
 * the auto-refreshing list of connections
 */
void curses_show_connections(void)
{
   DEBUG_MSG("curses_show_connections");

   /* if the object already exist, set the focus to it */
   if (wdg_connections) {
      wdg_set_focus(wdg_connections);
      return;
   }
   
   wdg_create_object(&wdg_connections, WDG_DYNLIST, WDG_OBJ_WANT_FOCUS);
   
   wdg_set_title(wdg_connections, "Live connections:", WDG_ALIGN_LEFT);
   wdg_set_size(wdg_connections, 1, 2, -1, SYSMSG_WIN_SIZE - 1);
   wdg_set_color(wdg_connections, WDG_COLOR_SCREEN, EC_COLOR);
   wdg_set_color(wdg_connections, WDG_COLOR_WINDOW, EC_COLOR);
   wdg_set_color(wdg_connections, WDG_COLOR_BORDER, EC_COLOR_BORDER);
   wdg_set_color(wdg_connections, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_color(wdg_connections, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_draw_object(wdg_connections);
 
   wdg_set_focus(wdg_connections);

   /* set the list print callback */
   wdg_dynlist_print_callback(wdg_connections, conntrack_print);
   
   /* set the select callback */
   wdg_dynlist_select_callback(wdg_connections, curses_connection_data);
  
   /* add the callback on idle to refresh the profile list */
   wdg_add_idle_callback(refresh_connections);

   /* add the destroy callback */
   wdg_add_destroy_key(wdg_connections, CTRL('Q'), curses_kill_connections);

   wdg_dynlist_add_callback(wdg_connections, 'd', curses_connection_detail);
   wdg_dynlist_add_callback(wdg_connections, 'k', curses_connection_kill);
}

static void curses_kill_connections(void)
{
   DEBUG_MSG("curses_kill_connections");
   wdg_del_idle_callback(refresh_connections);

   /* the object does not exist anymore */
   wdg_connections = NULL;
}

static void refresh_connections(void)
{
   /* if not focused don't refresh it */
   if (!(wdg_connections->flags & WDG_OBJ_FOCUSED))
      return;
   
   wdg_dynlist_refresh(wdg_connections);
}

/* 
 * details for a connection
 */
static void curses_connection_detail(void *conn)
{
   struct conn_tail *c = (struct conn_tail *)conn;
   char tmp[MAX_ASCII_ADDR_LEN];
   char *proto = "";
   
   DEBUG_MSG("curses_connection_detail");

   /* if the object already exist, set the focus to it */
   if (wdg_conn_detail) {
      wdg_destroy_object(&wdg_conn_detail);
      wdg_conn_detail = NULL;
   }
   
   wdg_create_object(&wdg_conn_detail, WDG_WINDOW, WDG_OBJ_WANT_FOCUS);
   
   wdg_set_title(wdg_conn_detail, "Connection detail:", WDG_ALIGN_LEFT);
   wdg_set_size(wdg_conn_detail, 1, 2, 70, 21);
   wdg_set_color(wdg_conn_detail, WDG_COLOR_SCREEN, EC_COLOR);
   wdg_set_color(wdg_conn_detail, WDG_COLOR_WINDOW, EC_COLOR);
   wdg_set_color(wdg_conn_detail, WDG_COLOR_BORDER, EC_COLOR_BORDER);
   wdg_set_color(wdg_conn_detail, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_color(wdg_conn_detail, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_draw_object(wdg_conn_detail);
 
   wdg_set_focus(wdg_conn_detail);
  
   /* add the destroy callback */
   wdg_add_destroy_key(wdg_conn_detail, CTRL('Q'), NULL);
   
   /* print the information */
   wdg_window_print(wdg_conn_detail, 1, 1, "Source MAC address      :  %s", mac_addr_ntoa(c->co->L2_addr1, tmp));
   wdg_window_print(wdg_conn_detail, 1, 2, "Destination MAC address :  %s", mac_addr_ntoa(c->co->L2_addr2, tmp));
   
   wdg_window_print(wdg_conn_detail, 1, 4, "Source IP address       :  %s", ip_addr_ntoa(&(c->co->L3_addr1), tmp));
   wdg_window_print(wdg_conn_detail, 1, 5, "Destination IP address  :  %s", ip_addr_ntoa(&(c->co->L3_addr2), tmp));
   
   switch (c->co->L4_proto) {
      case NL_TYPE_UDP:
         proto = "UDP";
         break;
      case NL_TYPE_TCP:
         proto = "TCP";
         break;
   }
   
   wdg_window_print(wdg_conn_detail, 1, 7, "Protocol                :  %s", proto);
   wdg_window_print(wdg_conn_detail, 1, 8, "Source port             :  %-5d  %s", ntohs(c->co->L4_addr1), service_search(c->co->L4_addr1, c->co->L4_proto));
   wdg_window_print(wdg_conn_detail, 1, 9, "Destination port        :  %-5d  %s", ntohs(c->co->L4_addr2), service_search(c->co->L4_addr2, c->co->L4_proto));
   
   wdg_window_print(wdg_conn_detail, 1, 11, "Transferred bytes       :  %d", c->co->xferred);
   
   if (c->co->DISSECTOR.user) {
      wdg_window_print(wdg_conn_detail, 1, 13, "Account                 :  %s / %s", c->co->DISSECTOR.user, c->co->DISSECTOR.pass);
      if (c->co->DISSECTOR.info)
         wdg_window_print(wdg_conn_detail, 1, 14, "Additional Info         :  %s", c->co->DISSECTOR.info);
   }
}

static void curses_connection_data(void *conn)
{
   struct conn_tail *c = (struct conn_tail *)conn;
   DEBUG_MSG("curses_connection_data");
  
   /* 
    * remove any hook on the open connection.
    * this is done to prevent a switch of connection
    * with the panel opened
    */
   if (curr_conn) {
      conntrack_hook_conn_del(curr_conn, split_print_po);
      conntrack_hook_conn_del(curr_conn, join_print_po);
   }
   
   /* set the global variable to pass the parameter to other functions */
   curr_conn = c->co;
   
   /* default is splitted view */
   curses_connection_data_split();
}

/*
 * show the content of the connection
 */
static void curses_connection_data_split(void)
{
   char tmp[MAX_ASCII_ADDR_LEN];
   char title[MAX_ASCII_ADDR_LEN+6];

   DEBUG_MSG("curses_connection_data_split");

   if (wdg_conndata) {
      wdg_destroy_object(&wdg_conndata);
   }

   wdg_create_object(&wdg_conndata, WDG_COMPOUND, WDG_OBJ_WANT_FOCUS);
   wdg_set_color(wdg_conndata, WDG_COLOR_SCREEN, EC_COLOR);
   wdg_set_color(wdg_conndata, WDG_COLOR_WINDOW, EC_COLOR);
   wdg_set_color(wdg_conndata, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_color(wdg_conndata, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_set_title(wdg_conndata, "Connection data", WDG_ALIGN_LEFT);
   wdg_set_size(wdg_conndata, 1, 2, -1, SYSMSG_WIN_SIZE - 1);
   
   wdg_create_object(&wdg_c1, WDG_SCROLL, 0);
   sprintf(title, "%s:%d", ip_addr_ntoa(&curr_conn->L3_addr1, tmp), ntohs(curr_conn->L4_addr1));
   wdg_set_title(wdg_c1, title, WDG_ALIGN_LEFT);
   wdg_set_color(wdg_c1, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_set_color(wdg_c1, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_size(wdg_c1, 2, 3, current_screen.cols / 2, SYSMSG_WIN_SIZE - 2);
   
   wdg_create_object(&wdg_c2, WDG_SCROLL, 0);
   sprintf(title, "%s:%d", ip_addr_ntoa(&curr_conn->L3_addr2, tmp), ntohs(curr_conn->L4_addr2));
   wdg_set_title(wdg_c2, title, WDG_ALIGN_LEFT);
   wdg_set_color(wdg_c2, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_set_color(wdg_c2, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_size(wdg_c2, current_screen.cols / 2 + 1, 3, -2, SYSMSG_WIN_SIZE - 2);

   /* set the buffers */
   wdg_scroll_set_lines(wdg_c1, GBL_CONF->connection_buffer / (current_screen.cols / 2));
   wdg_scroll_set_lines(wdg_c2, GBL_CONF->connection_buffer / (current_screen.cols / 2));
   
   /* link the widget together within the compound */
   wdg_compound_add(wdg_conndata, wdg_c1);
   wdg_compound_add(wdg_conndata, wdg_c2);
   
   /* add the destroy callback */
   wdg_add_destroy_key(wdg_conndata, CTRL('Q'), curses_destroy_conndata);
   
   wdg_compound_add_callback(wdg_conndata, 'j', curses_connection_data_join);
   wdg_compound_add_callback(wdg_conndata, 'y', curses_connection_inject);
   wdg_compound_add_callback(wdg_conndata, 'Y', curses_connection_inject_file);
   wdg_compound_add_callback(wdg_conndata, 'k', curses_connection_kill_wrapper);
   
   wdg_draw_object(wdg_conndata);
   wdg_set_focus(wdg_conndata);

   /* print the old data */
   connbuf_print(&curr_conn->data, split_print);

   /* add the hook on the connection to receive data only from it */
   conntrack_hook_conn_add(curr_conn, split_print_po);
}

static void curses_destroy_conndata(void)
{
   conntrack_hook_conn_del(curr_conn, split_print_po);
   conntrack_hook_conn_del(curr_conn, join_print_po);
   wdg_conndata = NULL;
   curr_conn = NULL;
}

static void split_print(u_char *text, size_t len, struct ip_addr *L3_src)
{
   int ret;

   /* use the global to reuse the same memory region */
   SAFE_REALLOC(dispbuf, hex_len(len) * sizeof(u_char) + 1);
   
   /* format the data */
   ret = GBL_FORMAT(text, len, dispbuf);
   dispbuf[ret] = 0;

   if (!ip_addr_cmp(L3_src, &curr_conn->L3_addr1))
      wdg_scroll_print(wdg_c1, EC_COLOR, "%s", dispbuf);
   else
      wdg_scroll_print(wdg_c2, EC_COLOR, "%s", dispbuf);
      
}

static void split_print_po(struct packet_object *po)
{
   int ret;
   
   /* if not focused don't refresh it */
   if (!(wdg_conndata->flags & WDG_OBJ_FOCUSED))
      return;
   
   /* use the global to reuse the same memory region */
   SAFE_REALLOC(dispbuf, hex_len(po->DATA.disp_len) * sizeof(u_char) + 1);
      
   /* format the data */
   ret = GBL_FORMAT(po->DATA.disp_data, po->DATA.disp_len, dispbuf);
   dispbuf[ret] = 0;
        
   if (!ip_addr_cmp(&po->L3.src, &curr_conn->L3_addr1))
      wdg_scroll_print(wdg_c1, EC_COLOR, "%s", dispbuf);
   else
      wdg_scroll_print(wdg_c2, EC_COLOR, "%s", dispbuf);
      
}

/*
 * show the data in a joined window 
 */
static void curses_connection_data_join(void)
{
   char src[MAX_ASCII_ADDR_LEN];
   char dst[MAX_ASCII_ADDR_LEN];
   char title[64];

   DEBUG_MSG("curses_connection_data_join");

   if (wdg_conndata) {
      wdg_destroy_object(&wdg_conndata);
   }

   wdg_create_object(&wdg_conndata, WDG_COMPOUND, WDG_OBJ_WANT_FOCUS);
   wdg_set_color(wdg_conndata, WDG_COLOR_SCREEN, EC_COLOR);
   wdg_set_color(wdg_conndata, WDG_COLOR_WINDOW, EC_COLOR);
   wdg_set_color(wdg_conndata, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_color(wdg_conndata, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_set_title(wdg_conndata, "Connection data", WDG_ALIGN_LEFT);
   wdg_set_size(wdg_conndata, 1, 2, -1, SYSMSG_WIN_SIZE - 1);
   
   wdg_create_object(&wdg_join, WDG_SCROLL, 0);
   sprintf(title, "%s:%d - %s:%d", ip_addr_ntoa(&curr_conn->L3_addr1, src), ntohs(curr_conn->L4_addr1),
                                 ip_addr_ntoa(&curr_conn->L3_addr2, dst), ntohs(curr_conn->L4_addr2));
   wdg_set_title(wdg_join, title, WDG_ALIGN_LEFT);
   wdg_set_color(wdg_join, WDG_COLOR_TITLE, EC_COLOR_TITLE);
   wdg_set_color(wdg_join, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_size(wdg_join, 2, 3, -2, SYSMSG_WIN_SIZE - 2);
   
   /* set the buffers */
   wdg_scroll_set_lines(wdg_join, GBL_CONF->connection_buffer / (current_screen.cols / 2) );
   
   /* link the widget together within the compound */
   wdg_compound_add(wdg_conndata, wdg_join);
   
   /* add the destroy callback */
   wdg_add_destroy_key(wdg_conndata, CTRL('Q'), curses_destroy_conndata);
  
   /* 
    * do not add inject callback because we can determine where to inject in
    * joined mode...
    */
   wdg_compound_add_callback(wdg_conndata, 'j', curses_connection_data_split);
   wdg_compound_add_callback(wdg_conndata, 'k', curses_connection_kill_wrapper);
   
   wdg_draw_object(wdg_conndata);
   wdg_set_focus(wdg_conndata);

   /* print the old data */
   connbuf_print(&curr_conn->data, join_print);

   /* add the hook on the connection to receive data only from it */
   conntrack_hook_conn_add(curr_conn, join_print_po);
}

static void join_print(u_char *text, size_t len, struct ip_addr *L3_src)
{
   int ret;
   
   /* use the global to reuse the same memory region */
   SAFE_REALLOC(dispbuf, hex_len(len) * sizeof(u_char) + 1);
   
   /* format the data */
   ret = GBL_FORMAT(text, len, dispbuf);
   dispbuf[ret] = 0;
   
   if (!ip_addr_cmp(L3_src, &curr_conn->L3_addr1))
      wdg_scroll_print(wdg_join, EC_COLOR_JOIN1, "%s", dispbuf);
   else
      wdg_scroll_print(wdg_join, EC_COLOR_JOIN2, "%s", dispbuf);
}

static void join_print_po(struct packet_object *po)
{
   int ret;

   /* if not focused don't refresh it */
   if (!(wdg_conndata->flags & WDG_OBJ_FOCUSED))
      return;
   
   /* use the global to reuse the same memory region */
   SAFE_REALLOC(dispbuf, hex_len(po->DATA.disp_len) * sizeof(u_char) + 1);
      
   /* format the data */
   ret = GBL_FORMAT(po->DATA.disp_data, po->DATA.disp_len, dispbuf);
   dispbuf[ret] = 0;
        
   if (!ip_addr_cmp(&po->L3.src, &curr_conn->L3_addr1))
      wdg_scroll_print(wdg_join, EC_COLOR_JOIN1, "%s", dispbuf);
   else
      wdg_scroll_print(wdg_join, EC_COLOR_JOIN2, "%s", dispbuf);
}

/*
 * kill the selected connection connection
 */
static void curses_connection_kill(void *conn)
{
   struct conn_object *c = (struct conn_object *)conn;
   
   DEBUG_MSG("curses_connection_kill");
  
   /* kill it */
   user_kill(curr_conn);
   
   /* set the status */
   c->status = CONN_KILLED;
   curses_message("The connection was killed !!");
}

/*
 * call the specialized funtion as this is a callback 
 * without the parameter
 */
static void curses_connection_kill_wrapper(void)
{
   curses_connection_kill(curr_conn);
}

/*
 * inject interactively with the user
 */
static void curses_connection_inject(void)
{
   wdg_t *in;
   
   DEBUG_MSG("curses_connection_inject");
   
   SAFE_REALLOC(injectbuf, 501 * sizeof(char));
   memset(injectbuf, 0, 501);
   
   wdg_create_object(&in, WDG_INPUT, WDG_OBJ_WANT_FOCUS | WDG_OBJ_FOCUS_MODAL);
   wdg_set_color(in, WDG_COLOR_SCREEN, EC_COLOR);
   wdg_set_color(in, WDG_COLOR_WINDOW, EC_COLOR);
   wdg_set_color(in, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_color(in, WDG_COLOR_TITLE, EC_COLOR_MENU);
   wdg_input_size(in, 75, 12);
   wdg_input_add(in, 1, 1, "Chars to be injected  :", injectbuf, 50, 10);
   wdg_input_set_callback(in, inject_user);
   
   wdg_draw_object(in);
      
   wdg_set_focus(in);
}

static void inject_user(void) 
{
   size_t len;

   /* escape the sequnces in the buffer */
   len = strescape(injectbuf, injectbuf);
   
   /* check where to inject */
   if (wdg_c1->flags & WDG_OBJ_FOCUSED) {
      user_inject(injectbuf, len, curr_conn, 1);
   } else if (wdg_c2->flags & WDG_OBJ_FOCUSED) {
      user_inject(injectbuf, len, curr_conn, 2);
   }
}

/*
 * inject form a file 
 */
static void curses_connection_inject_file(void)
{
   wdg_t *fop;
   
   DEBUG_MSG("curses_connection_inject_file");
   
   wdg_create_object(&fop, WDG_FILE, WDG_OBJ_WANT_FOCUS | WDG_OBJ_FOCUS_MODAL);
   
   wdg_set_title(fop, "Select a file to inject...", WDG_ALIGN_LEFT);
   wdg_set_color(fop, WDG_COLOR_SCREEN, EC_COLOR);
   wdg_set_color(fop, WDG_COLOR_WINDOW, EC_COLOR_MENU);
   wdg_set_color(fop, WDG_COLOR_FOCUS, EC_COLOR_FOCUS);
   wdg_set_color(fop, WDG_COLOR_TITLE, EC_COLOR_TITLE);

   wdg_file_set_callback(fop, inject_file);
   
   wdg_draw_object(fop);
   
   wdg_set_focus(fop);
}

/*
 * map the file into memory and pass the buffer to the inject function
 */
static void inject_file(char *path, char *file)
{
   char *filename;
   int fd;
   void *buf;
   size_t size;
   
   DEBUG_MSG("inject_file %s/%s", path, file);
   
   SAFE_CALLOC(filename, strlen(path)+strlen(file)+2, sizeof(char));

   sprintf(filename, "%s/%s", path, file);

   /* open the file */
   if ((fd = open(filename, O_RDONLY)) == -1) {
      ui_error("Can't load the file");
      return;
   }
      
   SAFE_FREE(filename);

   /* calculate the size of the file */
   size = lseek(fd, 0, SEEK_END);
   
   /* map it to the memory */
   buf = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
   if (buf == MAP_FAILED) {
      ui_error("Can't mmap the file");
      return;
   }

   /* check where to inject */
   if (wdg_c1->flags & WDG_OBJ_FOCUSED) {
      user_inject(buf, size, curr_conn, 1);
   } else if (wdg_c2->flags & WDG_OBJ_FOCUSED) {
      user_inject(buf, size, curr_conn, 2);
   }

   close(fd);
   munmap(buf, size);
   
}

/* EOF */

// vim:ts=3:expandtab

