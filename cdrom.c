/*
 * $Id: cdrom.c,v 1.4 1997/06/12 16:27:28 ograf Exp $
 *
 * cdrom utility functions for WMRack
 *
 * written by Oliver Graf <ograf@fga.de>
 *
 * some hints taken from WorkBone
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ustat.h>
#include <sys/time.h>
#include <fcntl.h>

#ifdef linux
#  include <linux/cdrom.h>
#else
#  include <sundev/srreg.h>
#endif /* linux */

#include "cdrom.h"

/* workaround -- used ints in MSF, linux uses u_char, what use other systems? */
#define msftoMSF(d,s) d.minute=s.minute; d.second=s.second; d.frame=s.frame;
#define MSFtocdmsf(d,s,e) d.cdmsf_min0=s.minute; d.cdmsf_sec0=s.second; d.cdmsf_frame0=s.frame; d.cdmsf_min1=e.minute; d.cdmsf_sec1=e.second; d.cdmsf_frame1=e.frame;

/*
 * cddb_sum(num)
 *
 * utility function to cddb_discid
 */
int cddb_sum(int n)
{
  char buf[12], *p;
  int  ret = 0;

  /* For backward compatibility this algorithm must not change */
  sprintf(buf,"%u",n);
  for (p=buf; *p!='\0'; p++)
    ret+=(*p-'0');
  return (ret);
}

/*
 * cddb_discid(cdinfo)
 *
 * calculates the id for a given cdinfo structure
 */
unsigned long cddb_discid(CDInfo *cdinfo)
{
  int i, t=0, n=0;

  /* For backward compatibility this algorithm must not change */
  for (i=0; i<cdinfo->tracks; i++) {
    n+=cddb_sum((cdinfo->track[i].toc.minute*60) + cdinfo->track[i].toc.second);
    t+=((cdinfo->track[i+1].toc.minute*60) + cdinfo->track[i+1].toc.second) -
      ((cdinfo->track[i].toc.minute*60) + cdinfo->track[i].toc.second);
  }
  cdinfo->discid=((n % 0xff) << 24 | t << 8 | cdinfo->tracks);
  return cdinfo->discid;
}

/*
 * cd_readTOC(CD)
 *
 * Read the table of contents from the CD.
 */
CDInfo *cd_readTOC(CD *cd)
{
  struct cdrom_tochdr   hdr;
  struct cdrom_tocentry entry;
  int                   i;
  CDInfo                *info;
  MSF                   tmp;

  if (cd->fd<0)
    return NULL;

  info=(CDInfo *)malloc(sizeof(CDInfo));
  if (info==NULL)
    {
      perror("cd_readTOC[malloc]");
      return NULL;
    }

  if (ioctl(cd->fd, CDROMREADTOCHDR, &hdr))
    {
      perror("cd_readTOC[readtochdr]");
      free(info);
      return NULL;
    }

  info->len=info->slen=0;
  info->start=hdr.cdth_trk0;
  info->end=hdr.cdth_trk1;
  info->tracks=hdr.cdth_trk1-hdr.cdth_trk0+1;
#ifdef DEBUG
  fprintf(stderr,"cd_readTOC[DEBUG]: read header with %d tracks\n",
	  info->tracks);
#endif

  info->track=malloc((info->tracks+1)*sizeof(TrackInfo));
  if (info->track==NULL)
    {
      perror("cd_readTOC[malloc]");
      free(info);
      return NULL;
    }
  for (i=0; i<=info->tracks; i++)
    {
      if (i==info->tracks)
	entry.cdte_track=CDROM_LEADOUT;
      else
	entry.cdte_track=info->start+i;
      entry.cdte_format=CDROM_MSF;
      if (ioctl(cd->fd, CDROMREADTOCENTRY, &entry))
	{
	  perror("cd_readTOC[tocentry read]");
	  free(info->track);
	  free(info);
	  return NULL;
	}

      info->track[i].num=i+1;
      msftoMSF(info->track[i].toc,entry.cdte_addr.msf);
      info->track[i].start=(((entry.cdte_addr.msf.minute*CD_SECS)+
			     entry.cdte_addr.msf.second)*CD_FRAMES+
			    entry.cdte_addr.msf.frame);
      info->track[i].len=-1;
      info->track[i].slen=-1;
      info->track[i].data=entry.cdte_ctrl&CDROM_DATA_TRACK?1:0;
    }

  /* Now compute actual track lengths. */
  for (i=0; i<info->tracks; i++)
    {
      info->track[i].len=info->track[i+1].start-info->track[i].start;
      info->track[i].slen=info->track[i].len/CD_FRAMES;
    }

  tmp=subMSF(info->track[info->tracks].toc,info->track[0].toc);
  info->len=msfFrames(tmp);
  info->slen=info->len/CD_FRAMES;
  cddb_discid(info);

  return info;
}

/*
 * cd_playMSF(cd,start,end)
 *
 * sends the actual play to the cdrom.
 */
int cd_playMSF(CD *cd, MSF start, MSF end)
{
  struct cdrom_msf msf;

  if (cd==NULL || cd->fd<0 || cd->info==NULL)
    return 1;

  MSFtocdmsf(msf,start,end);

  if (cd->info->current.mode==CDM_STOP)
    if (ioctl(cd->fd, CDROMSTART))
      {
	perror("cd_playMSF[CDROMSTART]");
	return 1;
      }

  if (ioctl(cd->fd, CDROMPLAYMSF, &msf))
    {
      printf("cd_playMSF[playmsf]\n");
      printf("  msf = %d:%d:%d %d:%d:%d\n",
	     msf.cdmsf_min0, msf.cdmsf_sec0, msf.cdmsf_frame0,
	     msf.cdmsf_min1, msf.cdmsf_sec1, msf.cdmsf_frame1);
      perror("cd_playMSF[CDROMPLAYMSF]");
      return 1;
    }
  return 0;
}

/*
 * cdinfo_free(info)
 *
 * frees a allocate CDInfo
 */
void cdinfo_free(CDInfo *info)
{
  if (info->track)
    free(info->track);
  free(info);
}

/*
 * cd_close(cd)
 *
 * closes the cd device and frees all data structures inside
 */
void cd_close(CD *cd)
{
  if (cd==NULL)
    return;

  if (cd->numcd)
    cdinfo_free(cd->info);
  free(cd->device);
  close(cd->fd);
  free(cd);
}

/*
 * cd_open(device)
 *
 * sets the device for further cd-access
 * set force to TRUE for immidiate change
 */
CD *cd_open(char *device, int noopen)
{
  struct stat st;
  CD *cd;

  if (stat(device,&st))
    {
      perror("cd_open[stat]");
      return NULL;
    }

  cd=(CD *)malloc(sizeof(CD));
  if (cd==NULL)
    {
      perror("cd_open[malloc]");
      return NULL;
    }

  cd->device=strdup(device);
  cd->fd=-1;
  cd->status=-1;
  cd->numcd=0;
  cd->info=NULL;

  if (!noopen)
    cd_reopen(cd);

  return cd;
}

/*
 * cd_suspend(cd)
 *
 * closes the cd device, but leaves all structures intact
 */
int cd_suspend(CD *cd)
{
  if (cd==NULL)
    return -1;

  close(cd->fd);
  cd->fd=-1;
  cd->status=-1;

  if (cd->info)
    memset(&cd->info->current,0xff,sizeof(CDPosition));

  return 0;
}

/*
 * cd_reopen(cd)
 *
 * reopens a suspended cd device, and reads the new TOC if cd is changed
 */
int cd_reopen(CD *cd)
{
  if (cd==NULL)
    return 1;

  if ((cd->fd=open(cd->device,0))<0)
    {
      perror("cd_reopen[open]");
      return 1;
    }

  if (cd->info)
    cdinfo_free(cd->info);

  cd->info=cd_readTOC(cd);
  if (cd->info!=NULL)
    {
      cd->status=1;
      cd->numcd=1;
      cd->info->play.start_track=cd->info->start;
      cd->info->play.end_track=cd->info->tracks;
    }
  else
    {
      cd->status=-1;
      cd->numcd=0;
      close(cd->fd);
      cd->fd=-1;
    }

  return 0;
}

/*
 * cd_status(cd,reopen)
 *
 * Return values:
 *  -1 error
 *   0 No CD in drive.
 *   1 CD in drive.
 *   2 CD has just been inserted (TOC has been read)
 *
 * Updates the CDPosition struct of the cd.
 * If reopen is not 0, the device is automatically reopened if needed.
 */
int cd_getStatus(CD *cd, int reopen)
{
  struct cdrom_subchnl sc;
  int                  ret=1, newcd=0;
  CDPosition           *cur;

  if (cd==NULL)
    return -1;

  if (cd->fd<0)
    {
      if (!reopen)
	{
	  cd->status=0;
	  return 0;
	}
      if (cd_reopen(cd))
	return -1;
      newcd=1;
    }

  if (cd->info==NULL)
    {
      cd->status=0;
      return 0;
    }

  cur=&cd->info->current;

  sc.cdsc_format=CDROM_MSF;

  if (ioctl(cd->fd, CDROMSUBCHNL, &sc))
    {
      memset(cur,0xff,sizeof(CDPosition));
      cur->mode=CDM_EJECT;
      cd->status=0;
      return 0;
    }

  if (newcd)
    {
      memset(cur,0xff,sizeof(CDPosition));
      cur->mode=CDM_STOP;
      ret=2;
    }

  switch (sc.cdsc_audiostatus)
    {
    case CDROM_AUDIO_PLAY:
      cur->mode=CDM_PLAY;

    setpos:
      cur->track=sc.cdsc_trk;
      cur->index=sc.cdsc_ind;

      msftoMSF(cur->relmsf,sc.cdsc_reladdr.msf);
      msftoMSF(cur->absmsf,sc.cdsc_absaddr.msf);

      break;

    case CDROM_AUDIO_PAUSED:
      cur->mode=CDM_PAUSE;
      goto setpos;

    case CDROM_AUDIO_COMPLETED:
      cur->mode=CDM_COMP;
      break;

    case CDROM_AUDIO_NO_STATUS:
      memset(cur,0xff,sizeof(CDPosition));
      cur->mode=CDM_STOP;
      break;
    }
  cd->status=ret;
  return ret;
}

/*
 * cd_doPlay(cd,start,end);
 *
 * start playing the cd. cd must be opened. play starts at track start goes
 * until end of track end.
 * 1 is the first track of the cd.
 */
int cd_doPlay(CD *cd, int start, int end)
{
  if (cd==NULL || cd->fd<0 || cd->info==NULL)
    return 1;
  if (start>end)
    {
      int d=end;
      end=start;
      start=d;
    }
  start-=cd->info->start;
  if (start<0) start=0;
  if (end>cd->info->tracks) end=cd->info->tracks; /* LEADOUT */
#ifdef DEBUG
  fprintf(stderr,"cd_doPlay[DEBUG]: play from [%d]%d:%d:%d to [%d]%d:%d:%d\n",
	  start,
	  cd->info->track[start].toc.minute,
	  cd->info->track[start].toc.second,
	  cd->info->track[start].toc.frame,
	  end,
	  cd->info->track[end].toc.minute,
	  cd->info->track[end].toc.second,
	  cd->info->track[end].toc.frame);
#endif
  cd->info->play.start_track=start+cd->info->start;
  cd->info->play.end_track=end;
  return cd_playMSF(cd,cd->info->track[start].toc,cd->info->track[end].toc);
}

/*
 * cd_doPause(cd)
 *
 * Pause the CD, if it's in play mode.
 * Resume the CD, If it's already paused.
 */
int cd_doPause(CD *cd)
{
  if (cd==NULL || cd->fd<0 || cd->info==NULL)
    return 1;

  switch (cd->info->current.mode) {
  case CDM_PLAY:
    cd->info->current.mode=CDM_PAUSE;
#ifdef DEBUG
    fprintf(stderr,"cd_doPause[DEBUG]: pausing cdrom\n");
#endif
    ioctl(cd->fd, CDROMPAUSE);
    break;
  case CDM_PAUSE:
    cd->info->current.mode=CDM_PLAY;
#ifdef DEBUG
    fprintf(stderr,"cd_doPause[DEBUG]: resuming cdrom\n");
#endif
    ioctl(cd->fd, CDROMRESUME);
    break;
#ifdef DEBUG
  default:
    fprintf(stderr,"cd_doPause[DEBUG]: not playing or pausing\n");
#endif
  }
  return 0;
}

/*
 * cd_doStop(cd)
 *
 * Stop the CD if it's not already stopped.
 */
int cd_doStop(CD *cd)
{
  if (cd==NULL || cd->fd<0 || cd->info==NULL)
    return 1;

  if (cd->info->current.mode!=CDM_STOP)
    {
#ifdef DEBUG
      fprintf(stderr,"cd_doStop[DEBUG]: stopping cdrom\n");
#endif
      if (ioctl(cd->fd, CDROMSTOP))
	{
	  perror("cd_doStop[CDROMSTOP]");
	  return 1;
	}
      if (cd->info->current.track>=cd->info->tracks)
	cd->info->play.start_track=cd->info->start;
      else
	cd->info->play.start_track=cd->info->current.track;
      memset(&cd->info->current,0xff,sizeof(CDPosition));
      cd->info->current.mode=CDM_STOP;
    }
  return 0;
}

/*
 * cd_doEject(CD *cd)
 *
 * Eject the current CD, if there is one.
 * Returns 0 on success, 1 if the CD couldn't be ejected, or 2 if the
 * CD contains a mounted filesystem.
 */
int cd_doEject(CD *cd)
{
  struct stat	stbuf;
  struct ustat	ust;

  if (cd==NULL || cd->fd<0 || cd->info==NULL)
    return 0;
  if (cd->info->current.mode==CDM_EJECT)
    return 0;

  if (fstat(cd->fd, &stbuf) != 0)
    {
      perror("cd_doEject[fstat]");
      return 1;
    }

  /* check for mounted filesystem */
  if (ustat(stbuf.st_rdev, &ust)==0)
    {
      perror("cd_doEject[mounted]");
      return 2;
    }

#ifdef DEBUG
  fprintf(stderr,"cd_doEject[DEBUG]: stopping cdrom\n");
#endif
  ioctl(cd->fd, CDROMSTOP);
#ifdef DEBUG
  fprintf(stderr,"cd_doEject[DEBUG]: ejecting cdrom\n");
#endif
  if (ioctl(cd->fd, CDROMEJECT))
    {
      perror("cd_doEject[CDROMEJECT]");
      return 1;
    }

  cd_suspend(cd);

  return 0;
}

/*
 * cd_doSkip(cd,seconds)
 *
 * Skip some seconds from current position
 */
int cd_doSkip(CD *cd, int secs)
{
  MSF start, end;

  if (cd==NULL || cd->fd<0 || cd->info==NULL)
    return 1;

  end=cd->info->track[cd->info->tracks].toc;
  start=cd->info->current.absmsf;
  start.second+=secs;
#ifdef DEBUG
  fprintf(stderr,"cd_doSkip[DEBUG]: skipping %d seconds\n",secs);
#endif
  while (start.second>59)
    {
      start.second-=60;
      start.minute++;
    }
  while (start.second<0)
    {
      start.second+=60;
      start.minute--;
    }
  if (cmpMSF(start,cd->info->track[0].toc)<0)
    {
      start=cd->info->track[0].toc;
#ifdef DEBUG
      fprintf(stderr,"cd_doSkip[DEBUG]: can't skip before first track\n");
#endif
    }

#ifdef DEBUG
  fprintf(stderr,"cd_doSkip[DEBUG]: play from %d:%d:%d to %d:%d:%d\n",
	  start.minute, start.second, start.frame,
	  end.minute, end.second, end.frame);
#endif
  return cd_playMSF(cd,start,end);
}

/*
 * cmpMSF(a,b)
 *
 * compares two MSF structs
 */
int cmpMSF(MSF a, MSF b)
{
  int fa=msfFrames(a), fb=msfFrames(b);
  if (fa<fb)
    return -1;
  else if (fa>fb)
    return 1;
  return 0;
}

/*
 * subMSF(a,b)
 *
 * subtract b from a
 */
MSF subMSF(MSF a, MSF b)
{
  MSF c;
  c.minute=a.minute-b.minute;
  c.second=a.second-b.second;
  c.frame =a.frame -b.frame;
  while (c.frame<0) {c.frame+=75; c.second--;}
  while (c.second<0) {c.second+=60; c.minute--;}
  if (c.minute<0) c.minute=0;
  return c;
}

#ifdef 0
#ifdef BOZO /* this seems to be sun/dec stuff */
/*
 * The minimum volume setting for the Sun and DEC CD-ROMs is 128 but for other
 * machines this might not be the case.
 */
int	min_volume = 128;
int	max_volume = 255;

/*
 * scale_volume(vol, max)
 *
 * Return a volume value suitable for passing to the CD-ROM drive.  "vol"
 * is a volume slider setting; "max" is the slider's maximum value.
 *
 * On Sun and DEC CD-ROM drives, the amount of sound coming out the jack
 * increases much faster toward the top end of the volume scale than it
 * does at the bottom.  To make up for this, we make the volume scale look
 * sort of logarithmic (actually an upside-down inverse square curve) so
 * that the volume value passed to the drive changes less and less as you
 * approach the maximum slider setting.  The actual formula looks like
 *
 *     (max^2 - (max - vol)^2) * (max_volume - min_volume)
 * v = --------------------------------------------------- + min_volume
 *                           max^2
 *
 * If your system's volume settings aren't broken in this way, something
 * like the following should work:
 *
 *	return ((vol * (max_volume - min_volume)) / max + min_volume);
 */
scale_volume(vol, max)
     int	vol, max;
{
  return ((max * max - (max - vol) * (max - vol)) *
	  (max_volume - min_volume) / (max * max) + min_volume);
}

/*
 * unscale_volume(cd_vol, max)
 *
 * Given a value between min_volume and max_volume, return the volume slider
 * value needed to achieve that value.
 *
 * Rather than perform floating-point calculations to reverse the above
 * formula, we simply do a binary search of scale_volume()'s return values.
 */
unscale_volume(cd_vol, max)
     int	cd_vol, max;
{
  int	vol, incr, scaled;

  for (vol = max / 2, incr = max / 4 + 1; incr; incr /= 2)
    {
      scaled = scale_volume(vol, max);
      if (cd_vol == scaled)
	break;
      if (cd_vol < scaled)
	vol -= incr;
      else
	vol += incr;
    }

  if (vol < 0)
    vol = 0;
  else if (vol > max)
    vol = max;

  return (vol);
}

/*
 * cd_volume(vol, bal, max)
 *
 * Set the volume levels.  "vol" and "bal" are the volume and balance knob
 * settings, respectively.  "max" is the maximum value of the volume knob
 * (the balance knob is assumed to always go from 0 to 20.)
 */
void
cd_volume(vol, bal, max)
     int	vol, bal, max;
{
  int	left, right;
  struct cdrom_volctrl v;

  /*
   * Set "left" and "right" to volume-slider values accounting for the
   * balance setting.
   *
   * XXX - the maximum volume setting is assumed to be in the 20-30 range.
   */
  if (bal < 9)
    right = vol - (9 - bal) * 2;
  else
    right = vol;
  if (bal > 11)
    left = vol - (bal - 11) * 2;
  else
    left = vol;

  /* Adjust the volume to make up for the CD-ROM drive's weirdness. */
  left = scale_volume(left, max);
  right = scale_volume(right, max);

  v.channel0 = left < 0 ? 0 : left > 255 ? 255 : left;
  v.channel1 = right < 0 ? 0 : right > 255 ? 255 : right;
  if (cd_fd >= 0)
    (void) ioctl(cd_fd, CDROMVOLCTRL, &v);
}

/*
 * Set the offset into the current track and play.  -1 means end of track
 * (i.e., go to next track.)
 */
void
play_from_pos(pos)
     int	pos;
{
  if (pos == -1)
    if (cd)
      pos = cd->trk[cur_track - 1].length - 1;
  if (cur_cdmode == 1)
    play_cd(cur_track, pos, playlist[cur_listno-1].end);
}

/* Try to keep the CD open all the time.  This is run in a subprocess. */
void
keep_cd_open()
{
  int	fd;
  struct flock	fl;
  extern	end;

  for (fd = 0; fd < 256; fd++)
    close(fd);

  if (fork())
    exit(0);

  if ((fd = open("/tmp/cd.lock", O_RDWR | O_CREAT, 0666)) < 0)
    exit(0);
  fl.l_type = F_WRLCK;
  fl.l_whence = 0;
  fl.l_start = 0;
  fl.l_len = 0;
  if (fcntl(fd, F_SETLK, &fl) < 0)
    exit(0);

  if (open(cd_device, 0) >= 0)
    {
      brk(&end);
      pause();
    }

  exit(0);
}

/*
 * find_trkind(track, index)
 *
 * Start playing at a particular track and index, optionally using a particular
 * frame as a starting position.  Returns a frame number near the start of the
 * index mark if successful, 0 if the track/index didn't exist.
 *
 * This is made significantly more tedious (though probably easier to port)
 * by the fact that CDROMPLAYTRKIND doesn't work as advertised.  The routine
 * does a binary search of the track, terminating when the interval gets to
 * around 10 frames or when the next track is encountered, at which point
 * it's a fair bet the index in question doesn't exist.
 */
find_trkind(track, index, start)
     int	track, index, start;
{
  int	top = 0, bottom, current, interval, ret = 0, i;

  if (cd == NULL || cd_fd < 0)
    return;

  for (i = 0; i < cur_ntracks; i++)
    if (cd->trk[i].track == track)
      break;
  bottom = cd->trk[i].start;

  for (; i < cur_ntracks; i++)
    if (cd->trk[i].track > track)
      break;

  top = i == cur_ntracks ? (cd->length - 1) * CD_FRAMES : cd->trk[i].start;

  if (start > bottom && start < top)
    bottom = start;

  current = (top + bottom) / 2;
  interval = (top - bottom) / 4;

  do {
    play_chunk(current, current + CD_FRAMES);

    if (cd_status() != 1)
      return (0);
    while (cur_frame < current)
      if (cd_status() != 1 || cur_cdmode != 1)
	return (0);
      else
	susleep(1);

    if (cd->trk[cur_track - 1].track > track)
      break;

    if (cur_index >= index)
      {
	ret = current;
	current -= interval;
      }
    else
      current += interval;
    interval /= 2;
  } while (interval > 2);

  return (ret);
}

/*
 * Simulate usleep() using select().
 */
susleep(usec)
     int	usec;
{
  struct timeval	tv;

  timerclear(&tv);
  tv.tv_sec = usec / 1000000;
  tv.tv_usec = usec % 1000000;
  return (select(0, NULL, NULL, NULL, &tv));
}
/*
 * Read the initial volume from the drive, if available.  Set cur_balance to
 * the balance level (0-20, 10=centered) and return the proper setting for
 * the volume knob.
 *
 * "max" is the maximum value of the volume knob.
 */
read_initial_volume(max)
     int max;
{
  int	left, right;

  /* Suns can't read the volume; oh well */
  left = right = 255;

  left = unscale_volume(left, max);
  right = unscale_volume(right, max);

  if (left < right)
    {
      cur_balance = (right - left) / 2 + 11;
      if (cur_balance > 20)
	cur_balance = 20;

      return (right);
    }
  else if (left == right)
    {
      cur_balance = 10;
      return (left);
    }
  else
    {
      cur_balance = (right - left) / 2 + 9;
      if (cur_balance < 0)
	cur_balance = 0;

      return (left);
    }
}
#endif /* BOZO */
#endif
