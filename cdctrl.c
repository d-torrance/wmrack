/*
 * $Id: cdctrl.c,v 1.2 1997/06/12 14:50:41 ograf Exp $
 *
 * Part of WMRack
 *
 * command line tool to control the cdrom
 * created to test the cdrom control stuff
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdrom.h"

char *commands[]={"info", "status", "play", "pause", "skip", "stop", "eject"};
#define NUM_CMD 7

void usage()
{
  fprintf(stderr,"Usage: cdrom [info|status|play|pause|skip|stop|eject] PARAMS\n");
}

int main(int argc, char **argv)
{
  CD *cd;
  int i, s=0, e=0xaa, status;

  if (argc<2)
    {
      usage();
      exit(EXIT_FAILURE);
    }

  for (i=0; i<NUM_CMD; i++)
    if (strcmp(argv[1],commands[i])==0)
      break;
  if (i==NUM_CMD)
    {
      usage();
      exit(EXIT_FAILURE);
    }

  cd=cd_open("/dev/hdd",0);
  if (cd->info==NULL)
    printf("No CDROM in drive\n");
  else
    status=cd_getStatus(cd,0);

  switch (i)
    {
    case 0:
      printf("discid: %08lX\n",cd->info->discid);
      printf("tracks: %d length: %dsec (%df)\n",
	     cd->info->tracks,cd->info->slen,cd->info->len);
      for (i=0; i<cd->info->tracks; i++)
	{
	  printf("%2d[%2d]: start(%10d) length(%10d f, %4d sec) %s\n",i,
		 cd->info->track[i].num,
		 cd->info->track[i].start,
		 cd->info->track[i].len,
		 cd->info->track[i].slen,
		 cd->info->track[i].data?"DATA":"");
	}
      break;
    case 1:
      printf("status is %d\n",status);
      printf("mode %d track %d ind %d\n",
	     cd->info->current.mode,
	     cd->info->current.track,
	     cd->info->current.index);
      i=cd->info->current.track;
      if (i!=-1)
	printf("%d[%d]: s%d l%d sl%d d%d\n",i,
	       cd->info->track[i].num,
	       cd->info->track[i].start,
	       cd->info->track[i].len,
	       cd->info->track[i].slen,
	       cd->info->track[i].data);
      else
	printf("no current track\n");
      printf("rel %d:%d:%d abs %d:%d:%d\n",
	     cd->info->current.relmsf.minute,
	     cd->info->current.relmsf.second,
	     cd->info->current.relmsf.frame,
	     cd->info->current.absmsf.minute,
	     cd->info->current.absmsf.second,
	     cd->info->current.absmsf.frame);
      break;
    case 2:
      if (argc>2)
	s=atoi(argv[2]);
      if (argc>3)
	e=atoi(argv[3]);
      cd_doPlay(cd,s,e);
      break;
    case 3:
      cd_doPause(cd);
      break;
    case 4:
      if (argc!=3)
	{
	  fprintf(stderr,"skip needs only one arg (seconds)\n");
	  exit(EXIT_FAILURE);
	}
      s=atoi(argv[2]);
      cd_doSkip(cd,s);
      break;
    case 5:
      cd_doStop(cd);
      break;
    case 6:
      cd_doEject(cd);
      break;
    default:
      usage();
      exit(EXIT_FAILURE);
    }
  cd_close(cd);
  return EXIT_SUCCESS;
}
