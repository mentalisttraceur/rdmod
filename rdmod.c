/*
 * rdmod - Command-Line R&D Mode Control
 * Copyright (C) 2011-2013 Alexander Kozhevnikov <mentalisttraceur@gmail.com>
 * Based on work by Faheem Pervez ("qwerty12")
 * This program depends on the Nokia N900 system library libcal. Many thanks
 * to Ivaylo Dimitrov ("freemangordon") for providing an open source reverse
 * engineered version, which freed this project from depending on the closed
 * source one. It can be found at: <https://gitorious.org/community-ssu/libcal>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>,
 * or write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330 Boston MA 02111-1307 USA.
 */

#include <cal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* For ensuring /dev/shm is mounted; it isn't very early during boot up, but
the semaphore functions used in libcal need it. */
/* cal-tool works around /dev/shm being absent by somehow putting semaphores
into /tmp instead. Would be appreciated if anyone knows how. */
#include <sys/mount.h>
/* To mkdir /dev/shm without writing to the rootfs, need to mount /dev as tmpfs
as well, which hides what's in /dev, so we also need mknod. */
#include <sys/types.h>
#include <sys/stat.h>

#define RDMOD_GOOD 0
#define RDMOD_NO_ARG -1
#define RDMOD_DEVSHM_OPEN_E 1
#define RDMOD_DEVSHM_READ_E 2
#define RDMOD_DEVSHM_MOUNT_E 3
#define RDMOD_DEVSHM_MKDIR_E 4
#define RDMOD_DEVMTD_MKNOD_E 5
#define RDMOD_DEV_OPEN_E 6
#define RDMOD_DEV_READ_E 7
#define RDMOD_DEV_MOUNT_E 8
#define RDMOD_CAL_INIT_E 9
#define RDMOD_CAL_READ_E 10
#define RDMOD_CAL_WRITE_E 11

int streamHasStr(FILE *, char const *);

char const * const mounts = "/proc/self/mounts";
char const * const devshm = "/dev/shm";
char const * const devmtd = "/dev/mtd1";
char const * const dev = "/dev";
char const * const devtmpfs = "/dev tmpfs";
char const * const rd_mode_flags[] =
{
 "master",
 "no-omap-wd",
 "no-ext-wd",
 "no-lifeguard-reset",
 "serial-console",
 "no-usb-timeout",
 "sti-console",
 "no-charging",
 "force-power-key"
};
char const * const help_text[] =
{
 "R&D Mode Control",
 "  -q\t\tQuery current R&D Mode flags",
 "  -e\t\tEnable R&D Mode",
 "  -d\t\tDisable R&D Mode",
 "  -s [flags]\tSet specified R&D Mode flags",
 "  -c [flags]\tClear specified R&D Mode flags",
 "  -h\t\tThis help text",
 "  -l\t\tList the valid R&D Mode flags",
 "  -p\t\tPrint the literal R&D Mode CAL area string",
 "  -w [string]\tWrite string directly to the R&D Mode CAL area"
};

int main(int argc, char * argv[])
{
 if(argc == 0)
 {
  return RDMOD_NO_ARG;
 }
 if(argc == 1)
 {
  for(size_t i = 0; i < 10; i+=1)
  {
   printf("%s\n", help_text[i]);
  }
  printf("Valid default R&D Mode flags:\n");
  for(size_t i = 1; i < 9; i+=1)
  {
   printf("  %s\n", rd_mode_flags[i]);
  }
  return RDMOD_GOOD;
 }
 
 /* Note: Maemo 5's /proc/mounts is a symlink to /proc/self/mounts anyway */
 FILE * mountsfile = fopen(mounts, "r");
 if(!mountsfile)
 {
  printf("Error opening %s, cannot determine if %s exists.\n", mounts, devshm);
  return RDMOD_DEVSHM_OPEN_E;
 }

 _Bool neededshm = 0, neededdev = 0;
 int tempint = streamHasStr(mountsfile, devshm);
 if(tempint == EOF)
 {
  printf("Error reading %s, cannot determine if %s exists.\n", mounts, devshm);
  fclose(mountsfile);
  return RDMOD_DEVSHM_READ_E;
 }
 else if(!tempint)
 {
  /* If no /dev/shm tmpfs, check if /dev is a tmpfs for that matter. */
  fseek(mountsfile, 0, SEEK_SET);
  if(!mountsfile)
  {
   printf("Error opening %s, cannot determine if %s exists.\n", mounts, dev);
   return RDMOD_DEV_OPEN_E;
  }
  int tempint = streamHasStr(mountsfile, devtmpfs);
  {
   if(tempint == EOF)
   {
    printf("Error reading %s, cannot determine if %s exists.\n", mounts, dev);
    fclose(mountsfile);
    return RDMOD_DEV_READ_E;
   }
   else if(!tempint)
   {
    if(mount("none", dev, "tmpfs", MS_NOATIME, ""))
    {
     printf("Error mounting tmpfs %s to make temporary %s on.\n", dev, devshm);
     fclose(mountsfile);
     return RDMOD_DEV_MOUNT_E;
    }
    neededdev = 1;
    if(mkdir(devshm, S_IRWXU))
    {
     printf("Error making directory %s, unable to use semaphores.\n", devshm);
     tempint = RDMOD_DEVSHM_MKDIR_E;
     goto cleanup_files;
    }
    if(mknod(devmtd, S_IFCHR|S_IRUSR|S_IWUSR, makedev(90, 2)))
    {
     printf("Error making %s node, unable to open CAL partition.\n", devmtd);
     tempint = RDMOD_DEVMTD_MKNOD_E;
     goto cleanup_files;
    }
   }
  }
  if(mount("none", devshm, "tmpfs", MS_NOSUID|MS_NODEV|MS_NOATIME, ""))
  {
   printf("Error mounting %s, unable to use semaphores.\n", devshm);
   tempint = RDMOD_DEVSHM_MOUNT_E;
   goto cleanup_files;
  }
  neededshm = 1;
 }
 fclose(mountsfile);
 mountsfile = NULL;
 
 struct cal * cal_s;
 void * tmp_ptr = NULL;
 unsigned long len = 0;
 if(cal_init(&cal_s) < 0)
 {
  printf("Failed to init CAL.\n");
  return RDMOD_CAL_INIT_E;
 }
 tempint = cal_read_block(cal_s, "r&d_mode", &tmp_ptr, &len, CAL_FLAG_USER);
 if(tempint == CAL_ERROR_NOT_FOUND)
 {
  /* The device has never written a CAL area "r&d_mode" block, so no block was
  found with that name. Safe to proceed. */
 }
 else if(tempint != CAL_OK)
 {
  cal_finish (cal_s);
  printf ("Error trying to access R&D Mode area from CAL.\n");
  return RDMOD_CAL_READ_E;
 }
 
 char * restrict rd_mode_current;
 char * rd_mode_string;
 if(len < 1 && !tmp_ptr)
 {
  printf("R&D Mode area seems empty; R&D Mode has likely never been set.\n");
  /* Resist the temptation to optimize in a way that 'fuses' these pointers to
  the same location: if you do, then realloc() of one will leave the other
  dangling, and similarly importantly, the 'restrict' keyword above becomes a
  lie to the compiler; we can get more optimized machine code in theory if the
  compiler knows that rd_mode_current and rd_mode_string never point to the
  same thing. Nor should you malloc the region for both strings in one call,
  else free won't work on one of them, and will deallocate bohth though the
  other. */
  rd_mode_current = (char *) malloc(1);
  rd_mode_string = (char *) malloc(1);
  *rd_mode_current = '\0';
  *rd_mode_string = '\0';
 } else {
  /* len + 1 to make room for terminating null that C-strings need; libcal
  does not store strings in a C-style null-terminated manner. */
  rd_mode_current = (char *) malloc(len + 1);
  rd_mode_string = (char *) malloc(len + 1);
  memcpy(rd_mode_string, (char *) tmp_ptr, len);
  free(tmp_ptr);
  rd_mode_string[len] = '\0';
  strcpy(rd_mode_current, rd_mode_string);
 }

 size_t index = 1;
 while(index < argc)
 {
  if(!strcmp(argv[index], "-h"))
  {
   for(size_t i = 1; i < 10; ++i)
   {
    printf("%s\n", help_text[i]);
   }
  }
  if(!strcmp(argv[index], "-l"))
  {
   printf("Valid default R&D Mode flags:\n");
   for(size_t i = 1; i < 9; ++i)
   {
    printf("  %s\n", rd_mode_flags[i]);
   }
  }
  else if(!strcmp(argv[index], "-p"))
  {
   printf("%s\n", rd_mode_string);
  }
  else if(!strcmp(argv[index], "-e"))
  {
   rd_mode_string = (char *) realloc(rd_mode_string, (strlen(rd_mode_flags[0]) + 1));
   strcpy(rd_mode_string, rd_mode_flags[0]);
   printf("R&D Mode enabled.\n");
  }
  else if(!strcmp(argv[index], "-d"))
  {
   *rd_mode_string = '\0';
   printf("R&D Mode disabled.\n");
  }
  else if(!strcmp(argv[index], "-q"))
  {
   for(size_t i = 1; i < 9; ++i)
   {
    /* Using rd_mode_string instead of rd_mode_current makes it act like prior
    parameters already took effect. */
    if(strstr(rd_mode_string, rd_mode_flags[i]))
    {
     printf("%s flag is on.\n", rd_mode_flags[i]);
    }
    else
    {
     printf("%s flag is off.\n", rd_mode_flags[i]);
    }
   }
  }
  else if(!strcmp(argv[index], "-s"))
  {
   ++index;
   size_t i; /* Has to be scoped outside loop for final i == 9 check. */
   for(i = 1; index < argc && i < 9; ++i)
   {
    if(!strcmp(argv[index], rd_mode_flags[i]))
    {
     if(!strstr(rd_mode_string, rd_mode_flags[i]))
     {
      rd_mode_string = (char *) realloc(rd_mode_string, (strlen(rd_mode_string) + strlen(rd_mode_flags[i]) + 2));
      strcat(rd_mode_string, ",");
      strcat(rd_mode_string, rd_mode_flags[i]);
      printf("%s is now set.\n", rd_mode_flags[i]);
     }
     else
     {
      printf("%s was already set.\n", rd_mode_flags[i]);
     }
     i = 1; /* Reset i since we're moving on to the next argument */
     index += 1;
    }
   }
   if(i == 9)
   {
    --index;
   }
  }
  else if(!strcmp(argv[index], "-c"))
  {
   ++index;
   size_t i; /* Has to be scoped outside loop for final i == 9 check. */
   for(i = 1; index < argc && i < 9; ++i)
   {
    if(!strcmp(argv[index], rd_mode_flags[i]))
    {
     char * substring;
     if(substring = strstr(rd_mode_string, rd_mode_flags[i]))
     {
      strcpy((substring - 1), (substring + strlen(rd_mode_flags[i])));
      printf("%s is now cleared.\n", rd_mode_flags[i]);
     }
     else
     {
      printf("%s was already cleared.\n", rd_mode_flags[i]);
     }
     i = 1; /* Reset i since we're moving on to the next argument */
     index += 1;
    }
   }
   if(i == 9)
   {
    --index;
   }
  }
  else if(!strcmp(argv[index], "-w"))
  {
   ++index;
   if(index < argc)
   {
    rd_mode_string = (char *) realloc(rd_mode_string, (strlen(argv[index]) + 1));
    strcpy(rd_mode_string, argv[index]);
    printf("\"%s\" was written.\n", rd_mode_string);
   }
  }
  ++index;
 }
 
 if(strcmp(rd_mode_string, rd_mode_current))
 {
  if(cal_write_block(cal_s, "r&d_mode", rd_mode_string, strlen(rd_mode_string), CAL_FLAG_USER) < 0)
  {
   printf("Failed to write to the R&D Mode area of CAL.\n");
   tempint = RDMOD_CAL_WRITE_E;
   goto cleanup;
  }
 }
 tempint = RDMOD_GOOD;

cleanup:
 cal_finish(cal_s);
 if(rd_mode_string)
 {
  free(rd_mode_string);
 }
 else
 {
  printf("rd_mode_string is null!\nEverything is fine, but you should yell at the developer to check the code.\n");
 }
 if(rd_mode_current)
 {
  if(rd_mode_current != rd_mode_string)
  {
   free(rd_mode_current);
  }
  else
  {
   printf("rd_mode_current and rd_mode_string pointed to the same spot!\nEverything is fine, but you should yell at the developer to check the code.\n");
  }
 }
 else
 {
  printf("rd_mode_current is null!\n Everything is fine, but you should yell at the developer to check the code.\n");
 }
cleanup_files:
 if(mountsfile)
 {
  fclose(mountsfile);
 }
 if(neededshm)
 {
  umount(devshm);
 }
 if(neededdev)
 {
  umount(dev);
 }
 return tempint;
}

int streamHasStr(FILE * stream, char const * str)
{
 /* i and tempchar are never in simultaneous use. Union saves drop of memory.
 .. in theory anyway. In practice it's possible compiler gets confused and
 makes less optimal code than if they were separate variables. */
 union
 {
  size_t i;
  int tempchar;
 }
 u;

virtual_loop:
 do
 {
  u.tempchar = fgetc(stream);
 }
 while(u.tempchar != (int) str[0] && u.tempchar != EOF);
 if(feof(stream))
 {
  return 0;
 }
 if(ferror(stream))
 {
  return EOF;
 }
 for(u.i = 1; str[u.i] != '\0'; u.i+=1)
 {
  if(fgetc(stream) != (int) str[u.i])
  {
   goto virtual_loop;
  }
 }
 return 1;
}
