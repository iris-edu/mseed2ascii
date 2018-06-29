/***************************************************************************
 * mseed2ascii.c
 *
 * Convert miniSEED waveform data to ASCII formats
 *
 * Written by Chad Trabant, IRIS Data Management Center
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include <libmseed.h>

#ifndef NOFDZIP
#include "fdzipstream.h"
#endif

#define VERSION "2.4dev"
#define PACKAGE "mseed2ascii"

/* Maximum number of metadata fields per line */
#define MAXMETAFIELDS 17

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

struct metanode
{
  char *metafields[MAXMETAFIELDS];
  double dip;
  double scalefactor;
  hptime_t starttime;
  hptime_t endtime;
};

static int64_t writeascii (MSTrace *mst);
static int writedata (char *outbuffer, size_t outsize, char *outfile);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt, int dasharg);
static int readlistfile (char *listfile);
struct metanode *getmetadata (MSTrace *mst);
static int addmetadata (char *metaline);
static int readmetadata (char *metafile);
static struct listnode *addnode (struct listnode **listroot, void *key, int keylen,
				 void *data, int datalen);
static void usage (void);

static int    verbose      = 0;    /* Verbosity level */
static int    reclen       = -1;   /* Record length, -1 = autodetected */
static int    deriverate   = 0;    /* Use sample rate derived instead of the reported rate */
static int    indifile     = 0;    /* Individual file processing flag */
static char  *unitsstr     = "Counts"; /* Units to write into output headers */
static char  *outputfile   = 0;    /* Output file name for single file output */
static FILE  *ofp          = 0;    /* Output file pointer */
static int    outformat    = 1;    /* Output file format */
static int    headerformat = 1;    /* 1 = Simple ASCII, 2 = GeoCSV */
static int    slistcols    = 1;    /* Number of columns for sample list output */
static int    scaledata    = 0;    /* Scale data, inversly, by factor in metadata */
static double timetol      = -1.0; /* Time tolerance for continuous traces */
static double sampratetol  = -1.0; /* Sample rate tolerance for continuous traces */

static char *zipfile = 0;
#ifndef NOFDZIP
static ZIPstream *zstream = 0;
static ZIPentry *zentry = 0;
static int zipmethod = -1;
#endif

struct listnode *filelist = 0;     /* A list of input files */
struct listnode *metadata = 0;     /* List of stations and coordinates, etc. */

int
main (int argc, char **argv)
{
  MSTraceGroup *mstg = 0;
  MSTrace *mst;
  MSRecord *msr = 0;

  struct listnode *flp;

  int retcode;
  int64_t totalrecs = 0;
  int64_t totalsamps = 0;
  int totalfiles = 0;

#ifndef NOFDZIP
  FILE *zipfp;
  int zipfd;
  ssize_t writestatus = 0;
#endif /* NOFDZIP */

  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
    return -1;

  /* Init MSTraceGroup */
  mstg = mst_initgroup (mstg);

  /* Open the output file if specified */
  if ( outputfile )
  {
    if ( strcmp (outputfile, "-") == 0 )
    {
      ofp = stdout;
    }
    else if ( (ofp = fopen (outputfile, "wb")) == NULL )
    {
      fprintf (stderr, "Cannot open output file: %s (%s)\n",
               outputfile, strerror(errno));
      return -1;
    }
  }

#ifndef NOFDZIP
  /* Open & intialize output ZIP archive if needed */
  if (zipfile)
  {
    if (!strcmp (zipfile, "-")) /* Write ZIP to stdout */
    {
      if (verbose)
        fprintf (stderr, "Writing ZIP archive to stdout\n");

      zipfd = fileno (stdout);
    }
    else if ((zipfp = fopen (zipfile, "wb")) == NULL) /* Open output ZIP file */
    {
      fprintf (stderr, "Cannot open output file: %s (%s)\n",
               zipfile, strerror (errno));
      return -1;
    }
    else
    {
      if (verbose)
        fprintf (stderr, "Writing ZIP archive to %s\n", zipfile);

      zipfd = fileno (zipfp);
    }

    /* Initialize ZIP container */
    if ((zstream = zs_init (zipfd, zstream)) == NULL)
    {
      fprintf (stderr, "Error in zs_init()\n");
      return 1;
    }
  }
#endif /* NOFDZIP */

  /* Read input miniSEED files into MSTraceGroup */
  flp = filelist;
  while ( flp != 0 )
  {
    if ( verbose )
      fprintf (stderr, "Reading %s\n", flp->data);

    while ( (retcode = ms_readmsr(&msr, flp->data, reclen, NULL, NULL,
                                  1, 1, verbose-1)) == MS_NOERROR )
    {
      if ( verbose > 1)
        msr_print (msr, verbose - 2);

      mst_addmsrtogroup (mstg, msr, 1, timetol, sampratetol);

      totalrecs++;
      totalsamps += msr->samplecnt;
    }

    if ( retcode != MS_ENDOFFILE )
      fprintf (stderr, "Error reading %s: %s\n", flp->data, ms_errorstr(retcode));

    /* Make sure everything is cleaned up */
    ms_readmsr (&msr, NULL, 0, NULL, NULL, 0, 0, 0);

    /* If processing each file individually, write ASCII and reset */
    if ( indifile )
    {
      mst = mstg->traces;
      while ( mst )
      {
        writeascii (mst);
        mst = mst->next;
      }

      mstg = mst_initgroup (mstg);
    }

    totalfiles++;
    flp = flp->next;
  }

  if ( ! indifile )
  {
    mst = mstg->traces;
    while ( mst )
    {
      writeascii (mst);
      mst = mst->next;
    }
  }

  /* Make sure everything is cleaned up */
  mst_freegroup (&mstg);

  if ( ofp )
    fclose (ofp);

#ifndef NOFDZIP
  /* Finish output ZIP archive if needed */
  if (zipfile)
  {
    if (zs_finish (zstream, &writestatus))
    {
      fprintf (stderr, "Error finishing ZIP archive, write status: %lld\n",
               (long long int)writestatus);
    }

    zs_free (zstream);
  }
#endif /* NOFDZIP */

  if ( verbose )
    printf ("Files: %d, Records: %lld, Samples: %lld\n", totalfiles,
	    (long long int)totalrecs, (long long int)totalsamps);

  return 0;
}  /* End of main() */


/***************************************************************************
 * writeascii:
 *
 * Write data buffer to output file as ASCII.
 *
 * Returns the number of samples written or -1 on error.
 ***************************************************************************/
static int64_t
writeascii (MSTrace *mst)
{
  struct metanode *mn = NULL;
  BTime btime;
  char outfile[1024];
  char *outname = outputfile;
  char timestr[50];
  char srcname[50];
  char *samptype;
  char *delimiter = " ";
  char outbuffer[8192];
  int outsize;

  int month, mday;
  int col, cnt, samplesize;
  int64_t line;
  int64_t lines;
  void *sptr;
  int32_t *idata;
  float *fdata;
  double *ddata;

#ifndef NOFDZIP
  ssize_t writestatus = 0;
#endif /* NOFDZIP */

  if ( ! mst )
    return -1;

  if ( mst->numsamples == 0 || mst->samprate == 0.0 )
    return 0;

  /* Check reported versus derived sampling rates */
  if ( mst->starttime < mst->endtime )
  {
    hptime_t hptimeshift;
    hptime_t hpdelta;
    double samprate;

    /* Calculate difference between end time of last miniSEED record and the end time
     * as calculated based on the start time, reported sample rate and number of samples. */
    hptimeshift = llabs (mst->endtime - mst->starttime - (hptime_t)((mst->numsamples - 1) * HPTMODULUS / mst->samprate));

    /* Calculate high-precision sample period using reported sample rate */
    hpdelta = (hptime_t)(( mst->samprate ) ? (HPTMODULUS / mst->samprate) : 0.0);

    /* Test if time shift is beyond half a sample period */
    if ( hptimeshift > (hpdelta * 0.5) )
    {
      /* Derive sample rate from start and end times and number of samples */
      samprate = (double) (mst->numsamples - 1) * HPTMODULUS / (mst->endtime - mst->starttime);

      if ( deriverate )
      {
        if ( verbose )
          fprintf (stderr, "Using derived sample rate of %g over reported rate of %g\n",
                   samprate, mst->samprate);

        mst->samprate = samprate;
      }
      else
      {
        fprintf (stderr, "[%s.%s.%s.%s] Reported sample rate different than derived rate (%g versus %g)\n",
                 mst->network, mst->station, mst->location, mst->channel,
                 mst->samprate, samprate);
        fprintf (stderr, "   Consider using the -dr option to use the sample rate derived from the series\n");
      }
    }
  }

  if ( verbose )
    fprintf (stderr, "Writing ASCII for %.8s.%.8s.%.8s.%.8s\n",
	     mst->network, mst->station, mst->location, mst->channel);

  /* Generate source name, ISO time string and time components */
  if (headerformat == 1) /* For simple text include quality code */
    mst_srcname (mst, srcname, 1);
  else                   /* For GeoCSV exclude quality code */
    mst_srcname (mst, srcname, 0);
  ms_hptime2isotimestr (mst->starttime, timestr, 1);
  ms_hptime2btime (mst->starttime, &btime);
  ms_doy2md (btime.year, btime.day, &month, &mday);

  /* Set sample type description */
  if ( mst->sampletype == 'f' || mst-> sampletype == 'd' )
  {
    samptype = "FLOAT";
  }
  else if ( mst->sampletype == 'i' )
  {
    samptype = "INTEGER";
  }
  else if ( mst->sampletype == 'a' )
  {
    samptype = "ASCII";
  }
  else
  {
    fprintf (stderr, "Error, unrecognized sample type: '%c'\n",
             mst->sampletype);
    return -1;
  }

  /* Create output file name: Net.Sta.Loc.Chan.Qual.Year-Month-DayTHourMinSec.Subsec.[txt|csv] */
  snprintf (outfile, sizeof(outfile), "%s.%s.%s.%s.%c.%04d-%02d-%02dT%02d%02d%02d.%06d.%s",
            mst->network, mst->station, mst->location, mst->channel, mst->dataquality,
            btime.year, month, mday, btime.hour, btime.min, btime.sec,
            (int)(mst->starttime - (hptime_t)MS_HPTIME2EPOCH(mst->starttime) * HPTMODULUS),
            (headerformat == 1) ? "txt" : "csv");

  /* Generate and open output file name if single file not being used and no ZIP output */
  if ( ! ofp && ! zipfile )
  {
    /* Open output file */
    if ( (ofp = fopen (outfile, "wb")) == NULL )
    {
      fprintf (stderr, "Cannot open output file: %s (%s)\n",
               outfile, strerror(errno));
      return -1;
    }

    outname = outfile;
  }

  if ( (samplesize = ms_samplesize(mst->sampletype)) == 0 )
  {
    fprintf (stderr, "Unrecognized sample type: %c\n", mst->sampletype);
  }

  /* Search for matching metadata */
  if (metadata)
    mn = getmetadata (mst);

  /* Scale data samples if scale factor available
   * Integer data are converted to float
   * Units are taken from the metata */
  if (scaledata && mn && mn->metafields[11] && mn->scalefactor)
  {
    for (cnt = 0; cnt < mst->numsamples; cnt++)
    {
      idata = (int32_t *)mst->datasamples + cnt;
      fdata = (float *)mst->datasamples + cnt;
      ddata = (double *)mst->datasamples + cnt;

      /* Integers are converted to floats */
      if (mst->sampletype == 'i')
        *fdata = (float)*idata / mn->scalefactor;

      else if (mst->sampletype == 'f')
        *fdata = *fdata / mn->scalefactor;

      else if (mst->sampletype == 'd')
        *ddata = *ddata / mn->scalefactor;
    }

    if (mst->sampletype == 'i')
    {
      mst->sampletype = 'f';
      samptype = "FLOAT";
    }

    unitsstr = mn->metafields[13];
  }

#ifndef NOFDZIP
  /* Begin ZIP entry */
  if (zipfile)
  {
    if (!(zentry = zs_entrybegin (zstream, outfile, time (NULL),
                                  zipmethod, &writestatus)))
    {
      fprintf (stderr, "Cannot begin ZIP entry, write status: %lld\n",
               (long long int)writestatus);
      return -1;
    }
  }
#endif /* NOFDZIP */

  /* Simple ASCII header format:
   * "TIMESERIES Net_Sta_Loc_Chan_Qual, ## samples, ## sps, isotime, SLIST|TSPAIR, INTEGER|FLOAT|ASCII, Units"
   *
   * GeoCSV header format:
   * "# dataset: GeoCSV 2.0"
   * "# delimiter: <DELIMITER>"
   * "# SID: <SOURCE ID>"
   * "# sample_count: <COUNT>"
   * "# sample_rate_hz: <RATE>"
   * "# start_time: <RATE>"

   * If metadata is present these may be included:
   * "# latitude_deg: <LATITUDE>"
   * "# longitude_deg: <LONGITUDE>"
   * "# elevation_m: <ELEVATION>"
   * "# depth_m: <DEPTH>"
   * "# azimuth_deg: <AZIMUTH>"
   * "# dip_deg: <DIP>"
   * "# instrument: <INSTRUMENT>"
   * "# scale_factor: <SCALEFACTOR>"
   * "# scale_frequency: <SCALEFREQ>"
   * "# scale_units: <SCALEUNITS>"

   * "# field_unit: UTC, <TYPE>"
   * "# field_type: datetime, <TYPE>"
   */

  /* Create initial part of header */
  if (headerformat == 1)
  {
    delimiter = " ";

    /* Simple text header */
    outsize = snprintf (outbuffer, sizeof(outbuffer),
                        "TIMESERIES %s, %lld samples, %g sps, %s, ",
                        srcname, (long long int)mst->numsamples, mst->samprate, timestr);
  }
  else
  {
    delimiter = ",";

    /* GeoCSV header */
    outsize = snprintf (outbuffer, sizeof(outbuffer),
                        "# dataset: GeoCSV 2.0\n"
                        "# delimiter: %s\n"
                        "# SID: %s\n"
                        "# sample_count: %lld\n"
                        "# sample_rate_hz: %g\n"
                        "# start_time: %sZ\n",
                        delimiter,
                        srcname,
                        (long long int)mst->numsamples,
                        mst->samprate,
                        timestr);

    if (mn)
    {
      if (mn->metafields[4])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# latitude_deg: %s\n", mn->metafields[4]);
      if (mn->metafields[5])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# longitude_deg: %s\n", mn->metafields[5]);
      if (mn->metafields[6])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# elevation_m: %s\n", mn->metafields[6]);
      if (mn->metafields[7])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# depth_m: %s\n", mn->metafields[7]);
      if (mn->metafields[8])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# azimuth_deg: %s\n", mn->metafields[8]);
      if (mn->metafields[9])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# dip_deg: %s\n", mn->metafields[9]);
      if (mn->metafields[10])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# instrument: %s\n", mn->metafields[10]);
      if (mn->metafields[11])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# scale_factor: %s\n", mn->metafields[11]);
      if (mn->metafields[12])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# scale_frequency_hz: %s\n", mn->metafields[12]);
      if (mn->metafields[13])
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize,
                             "# scale_units: %s\n", mn->metafields[13]);
    }
  }

  if (outsize > sizeof(outbuffer))
    outsize = sizeof(outbuffer);

  if (writedata (outbuffer, outsize, outfile))
    return -1;

  if ( outformat == 1 || mst->sampletype == 'a' )
  {
    if ( verbose > 1 )
      fprintf (stderr, "Writing ASCII sample list file: %s\n", outname);

    /* Finish header */
    if (headerformat == 1)
    {
      outsize = snprintf (outbuffer, sizeof(outbuffer),
                          "SLIST, %s, %s\n", samptype, unitsstr);
    }
    else
    {
      /* GeoCSV sample list can only be a single column */
      slistcols = 1;

      outsize = snprintf (outbuffer, sizeof(outbuffer),
                          "# field_unit: %s\n"
                          "# field_type: %s\n"
                          "Sample\n",
                          unitsstr,
                          samptype);
    }

    if (outsize > sizeof(outbuffer))
      outsize = sizeof(outbuffer);

    if (writedata (outbuffer, outsize, outfile))
      return -1;

    lines = (mst->numsamples / slistcols) + ((slistcols == 1) ? 0 : 1);

    outsize = 0;

    if ( mst->sampletype == 'a' )
    {
      if (writedata (mst->datasamples, (size_t)mst->numsamples, outfile))
        return -1;
      if (writedata ("\n", 1, outfile))
        return -1;
    }
    else
    {
      outsize = 0;
      for ( cnt = 0, line = 0; line < lines; line++ )
      {
        for ( col = 1; col <= slistcols ; col ++ )
        {
          if ( cnt < mst->numsamples )
          {
            sptr = (char*)mst->datasamples + (cnt * samplesize);

            if (mst->sampletype == 'i')
            {
              if (col != slistcols)
                outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize,
                                     "%-10d  ", *(int32_t *)sptr);
              else
                outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize,
                                     "%d", *(int32_t *)sptr);
            }
            else if (mst->sampletype == 'f')
            {
              if ( col != slistcols )
                outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize,
                                     "%-10.8g  ", *(float *)sptr);
              else
                outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize,
                                     "%.8g", *(float *)sptr);
            }
            else if ( mst->sampletype == 'd' )
            {
              if ( col != slistcols )
                outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize,
                                     "%-10.10g  ", *(double *)sptr);
              else
                outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize,
                                     "%.10g", *(double *)sptr);
            }

            cnt++;
          }
        }

        outsize += snprintf (outbuffer + outsize, sizeof (outbuffer) - outsize, "\n");

        /* Write data if less than 1024 bytes available */
        if ((sizeof (outbuffer) - outsize) < 1024)
        {
          if (writedata (outbuffer, outsize, outfile))
            return -1;
          outsize = 0;
        }
      } /* Done looping through lines and samples */

      /* Flush any unwritten output */
      if (outsize > 0)
      {
        if (writedata (outbuffer, outsize, outfile))
          return -1;
        outsize = 0;
      }
    }
  }
  else if ( outformat == 2 )
  {
    hptime_t samptime = mst->starttime;
    double hpperiod = ( mst->samprate ) ? (HPTMODULUS / mst->samprate) : 0;

    if ( verbose > 1 )
      fprintf (stderr, "Writing ASCII time-sample pair file: %s\n", outname);

    /* Finish header */
    if (headerformat == 1)
    {
      outsize = snprintf (outbuffer, sizeof(outbuffer),
                          "TSPAIR, %s, %s\n", samptype, unitsstr);
    }
    else
    {
      outsize = snprintf (outbuffer, sizeof(outbuffer),
                          "# field_unit: UTC, %s\n"
                          "# field_type: datetime, %s\n"
                          "Time, Sample\n",
                          unitsstr,
                          samptype);
    }

    if (outsize > sizeof(outbuffer))
      outsize = sizeof(outbuffer);

    if (writedata (outbuffer, outsize, outfile))
      return -1;

    outsize = 0;
    for ( cnt = 0; cnt < mst->numsamples; cnt++ )
    {
      ms_hptime2isotimestr (samptime, timestr, 1);

      sptr = (char*)mst->datasamples + (cnt * samplesize);

      if ( mst->sampletype == 'i' )
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize, "%s%s%s %d\n",
                             timestr, (headerformat == 1) ? "" : "Z", delimiter, *(int32_t *)sptr);

      else if ( mst->sampletype == 'f' )
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize, "%s%s%s %.8g\n",
                             timestr, (headerformat == 1) ? "" : "Z", delimiter, *(float *)sptr);

      else if ( mst->sampletype == 'd' )
        outsize += snprintf (outbuffer + outsize, sizeof(outbuffer) - outsize, "%s%s%s %.10g\n",
                             timestr, (headerformat == 1) ? "" : "Z", delimiter, *(double *)sptr);

      samptime = mst->starttime + (hptime_t)((cnt+1) * hpperiod);

      /* Write data if less than 1024 bytes available */
      if ((sizeof (outbuffer) - outsize) < 1024)
      {
        if (writedata (outbuffer, outsize, outfile))
          return -1;
        outsize = 0;
      }
    } /* Done looping through samples */

    /* Flush any unwritten output */
    if (outsize > 0)
    {
      if (writedata (outbuffer, outsize, outfile))
        return -1;
      outsize = 0;
    }
  }
  else
  {
    fprintf (stderr, "Error, unrecognized format: '%d'\n", outformat);
  }

  if ( outname == outfile )
  {
    fclose (ofp);
    ofp = 0;
  }

#ifndef NOFDZIP
  /* End ZIP entry */
  if (zipfile)
  {
    if (!zs_entryend (zstream, zentry, &writestatus))
    {
      fprintf (stderr, "Error ending ZIP entry for %s, write status: %lld\n",
               outfile, (long long int)writestatus);
      return 1;
    }
  }

  zentry = 0;
#endif /* NOFDZIP */

  fprintf (stderr, "Wrote %lld samples for %s\n",
	   (long long int)mst->numsamples, srcname);

  return mst->numsamples;
}  /* End of writeascii() */

/***************************************************************************
 * writedata:
 *
 * Write data buffer to output destinations.
 *
 * Returns 0 on success or -1 on error.
 ***************************************************************************/
static int
writedata (char *outbuffer, size_t outsize, char *outfile)
{
#ifndef NOFDZIP
  ssize_t writestatus = 0;
#endif /* NOFDZIP */

  if (ofp)
  {
    if (fwrite (outbuffer, outsize, 1, ofp) != 1 )
    {
      fprintf (stderr, "Error adding entry data for %s to output file\n", outfile);
      return -1;
    }
  }

#ifndef NOFDZIP
  if (zipfile)
  {
    if (!zs_entrydata (zstream, zentry, (uint8_t *)outbuffer, outsize, &writestatus))
    {
      fprintf (stderr, "Error adding entry data for %s to output ZIP, write status: %lld\n",
               outfile, (long long int)writestatus);
      return -1;
    }
  }
#endif /* NOFDZIP */

  return 0;
}  /* End of writedata() */

/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure.
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  char *metafile = 0;
  char *metaline = 0;
  int optind;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
  {
    if (strcmp (argvec[optind], "-V") == 0)
    {
      fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
      exit (0);
    }
    else if (strcmp (argvec[optind], "-h") == 0)
    {
      usage();
      exit (0);
    }
    else if (strncmp (argvec[optind], "-v", 2) == 0)
    {
      verbose += strspn (&argvec[optind][1], "v");
    }
    else if (strcmp (argvec[optind], "-dr") == 0)
    {
      deriverate = 1;
    }
    else if (strcmp (argvec[optind], "-i") == 0)
    {
      indifile = 1;
    }
    else if (strcmp (argvec[optind], "-G") == 0)
    {
      headerformat = 2;
    }
    else if (strcmp (argvec[optind], "-c") == 0)
    {
      slistcols = strtoul (getoptval(argcount, argvec, optind++, 0), NULL, 10);
    }
    else if (strcmp (argvec[optind], "-u") == 0)
    {
      unitsstr = getoptval(argcount, argvec, optind++, 0);
    }
    else if (strcmp (argvec[optind], "-m") == 0)
    {
      metafile = getoptval (argcount, argvec, optind++, 0);
    }
    else if (strcmp (argvec[optind], "-M") == 0)
    {
      metaline = getoptval (argcount, argvec, optind++, 0);
      if ( addmetadata(metaline) < 0 )
      {
        fprintf (stderr, "Error adding metadata fields for line:\n%s\n", metaline);
      }
    }
    else if (strcmp (argvec[optind], "-s") == 0)
    {
      scaledata = 1;
    }
    else if (strcmp (argvec[optind], "-f") == 0)
    {
      outformat = strtoul (getoptval(argcount, argvec, optind++, 0), NULL, 10);
    }
    else if (strcmp (argvec[optind], "-o") == 0)
    {
      outputfile = getoptval(argcount, argvec, optind++, 1);
    }
#ifndef NOFDZIP
    else if (strcmp (argvec[optind], "-z") == 0)
    {
      zipfile = getoptval (argcount, argvec, optind++, 1);
      zipmethod = ZS_DEFLATE;
    }
    else if (strcmp (argvec[optind], "-z0") == 0)
    {
      zipfile = getoptval (argcount, argvec, optind++, 1);
      zipmethod = ZS_STORE;
    }
#endif
    else if (strcmp (argvec[optind], "-r") == 0)
    {
      reclen = strtoul (getoptval(argcount, argvec, optind++, 0), NULL, 10);
    }
    else if (strcmp (argvec[optind], "-tt") == 0)
    {
      timetol = strtod (getoptval(argcount, argvec, optind++, 0), NULL);
    }
    else if (strcmp (argvec[optind], "-rt") == 0)
    {
      sampratetol = strtod (getoptval(argcount, argvec, optind++, 0), NULL);
    }
    else if (strncmp (argvec[optind], "-", 1) == 0 &&
             strlen (argvec[optind]) > 1 )
    {
      fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
    else
    {
      /* Add the file name to the intput file list */
      if ( ! addnode (&filelist, NULL, 0, argvec[optind], strlen(argvec[optind])+1) )
      {
        fprintf (stderr, "Error adding file name to list\n");
      }
    }
  }

  /* Make sure input files were specified */
  if ( filelist == 0 )
  {
    fprintf (stderr, "No input files were specified\n\n");
    fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
    fprintf (stderr, "Try %s -h for usage\n", PACKAGE);
    exit (1);
  }

  /* Report the program version */
  if ( verbose )
    fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);

  /* Sanity check the number of columns */
  if (slistcols > 100)
  {
    fprintf (stderr, "\nToo many data sample columns specified: %d\n", slistcols);
    exit (1);
  }

  /* Check the input files for any list files, if any are found
   * remove them from the list and add the contained list */
  if ( filelist )
  {
    struct listnode *prevln, *ln;
    char *lfname;

    prevln = ln = filelist;
    while ( ln != 0 )
    {
      lfname = ln->data;

      if ( *lfname == '@' )
      {
        /* Remove this node from the list */
        if ( ln == filelist )
          filelist = ln->next;
        else
          prevln->next = ln->next;

        /* Skip the '@' first character */
        if ( *lfname == '@' )
          lfname++;

        /* Read list file */
        readlistfile (lfname);

        /* Free memory for this node */
        if ( ln->key )
          free (ln->key);
        free (ln->data);
        free (ln);
      }
      else
      {
        prevln = ln;
      }

      ln = ln->next;
    }
  }

  /* Read metadata file if specified */
  if (metafile)
  {
    if (readmetadata (metafile))
    {
      fprintf (stderr, "Error reading metadata file\n");
      return -1;
    }
  }

  return 0;
}  /* End of parameter_proc() */


/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 * dasharg: flag indicating if the value can beging with a dash (-)
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt, int dasharg)
{
  if ( argvec == NULL || argvec[argopt] == NULL ) {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }

  /* When the value potentially starts with a dash (-) */
  if ( (argopt+1) < argcount && dasharg )
    return argvec[argopt+1];

  /* Otherwise check that the value is not another option */
  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];

  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
  return 0;
}  /* End of getoptval() */


/***************************************************************************
 * readlistfile:
 *
 * Read a list of files from a file and add them to the filelist for
 * input data.  The filename is expected to be the last
 * space-separated field on the line.
 *
 * Returns the number of file names parsed from the list or -1 on error.
 ***************************************************************************/
static int
readlistfile (char *listfile)
{
  FILE *fp;
  char  line[1024];
  char *ptr;
  int   filecnt = 0;

  char  filename[1024];
  char *lastfield = 0;
  int   fields = 0;
  int   wspace;

  /* Open the list file */
  if ( (fp = fopen (listfile, "rb")) == NULL )
  {
    if (errno == ENOENT)
    {
      fprintf (stderr, "Could not find list file %s\n", listfile);
      return -1;
    }
    else
    {
      fprintf (stderr, "Error opening list file %s: %s\n",
               listfile, strerror (errno));
      return -1;
    }
  }
  if ( verbose )
    fprintf (stderr, "Reading list of input files from %s\n", listfile);

  while ( (fgets (line, sizeof(line), fp)) !=  NULL)
  {
    /* Truncate line at first \r or \n, count space-separated fields
     * and track last field */
    fields = 0;
    wspace = 0;
    ptr = line;
    while ( *ptr )
    {
      if ( *ptr == '\r' || *ptr == '\n' || *ptr == '\0' )
      {
        *ptr = '\0';
        break;
      }
      else if ( *ptr != ' ' )
      {
        if ( wspace || ptr == line )
        {
          fields++; lastfield = ptr;
        }
        wspace = 0;
      }
      else
      {
        wspace = 1;
      }

      ptr++;
    }

    /* Skip empty lines */
    if ( ! lastfield )
      continue;

    if ( fields >= 1 && fields <= 3 )
    {
      fields = sscanf (lastfield, "%s", filename);

      if ( fields != 1 )
      {
        fprintf (stderr, "Error parsing file name from: %s\n", line);
        continue;
      }

      if ( verbose > 1 )
        fprintf (stderr, "Adding '%s' to input file list\n", filename);

      /* Add file name to the intput file list */
      if ( ! addnode (&filelist, NULL, 0, filename, strlen(filename)+1) )
      {
        fprintf (stderr, "Error adding file name to list\n");
      }

      filecnt++;

      continue;
    }
  }

  fclose (fp);

  return filecnt;
}  /* End readlistfile() */

/***************************************************************************
 * getmetadata:
 *
 * Search the metadata list for the first matching source and return
 * if found.  The source names (net, sta, loc, chan) are used to find
 * a match.  If metadata list entries include a '*' they will match
 * everything, for example if the channel field is '*' all channels
 * for the specified network, station and location will match the list
 * entry.
 *
 * Returns matching metadata node if match found, NULL otherwise.
 ***************************************************************************/
struct metanode *
getmetadata (MSTrace *mst)
{
  struct listnode *mlp = metadata;
  struct metanode *mn = NULL;

  if (!mst)
    return NULL;

  while (mlp)
  {
    mn = (struct metanode *)mlp->data;

    /* Sanity check that source name fields are present */
    if (!mn->metafields[0] || !mn->metafields[1] ||
        !mn->metafields[2] || !mn->metafields[3])
    {
      fprintf (stderr, "getmetadata(): error, source name fields not all present\n");
    }
    /* Test if network, station, location and channel; also handle simple wildcards */
    else if ((!strcmp (mst->network, mn->metafields[0]) || (*(mn->metafields[0]) == '*')) &&
             (!strcmp (mst->station, mn->metafields[1]) || (*(mn->metafields[1]) == '*')) &&
             (!strcmp (mst->location, mn->metafields[2]) || (*(mn->metafields[2]) == '*')) &&
             (!strcmp (mst->channel, mn->metafields[3]) || (*(mn->metafields[3]) == '*')))
    {
      /* Check time window match */
      if (mn->starttime != HPTERROR || mn->endtime != HPTERROR)
      {
        /* Check for overlap with metadata window */
        if (mn->starttime != HPTERROR && mn->endtime != HPTERROR)
        {
          if (!(mst->endtime >= mn->starttime && mst->starttime <= mn->endtime))
          {
            mlp = mlp->next;
            continue;
          }
        }
        /* Check if data after start time */
        else if (mn->starttime != HPTERROR)
        {
          if (mst->endtime < mn->starttime)
          {
            mlp = mlp->next;
            continue;
          }
        }
        /* Check if data before end time */
        else if (mn->endtime != HPTERROR)
        {
          if (mst->starttime > mn->endtime)
          {
            mlp = mlp->next;
            continue;
          }
        }
      }

      if (verbose > 1)
        fprintf (stderr, "Found metadata for N: '%s', S: '%s', L: '%s', C: '%s' (%s - %s)\n",
                 mst->network, mst->station, mst->location, mst->channel,
                 (mn->metafields[15]) ? mn->metafields[15] : "NONE",
                 (mn->metafields[16]) ? mn->metafields[16] : "NONE");

      return mn;
    }

    mlp = mlp->next;
  }

  return NULL;
} /* End of getmetadata() */


/***************************************************************************
 * addmetadata:
 *
 * Parse and add a metadata entry into a structured list.  The
 * metadata line should contain the following fields (comma or bar
 * separated) in this order:
 *
 * The metadata list should be populated with an array of pointers to:
 *  0:  Network
 *  1:  Station
 *  2:  Location
 *  3:  Channel
 *  4:  Latitude
 *  5:  Longitude
 *  6:  Elevation
 *  7:  Depth
 *  8:  Component Azimuth
 *  9:  Component Dip (SEED) or Inclination (SAC)
 *  10: Instrument Name
 *  11: Scale Factor
 *  12: Scale Frequency
 *  13: Scale Units
 *  14: Sampling rate
 *  15: Start time
 *  16: End time
 *
 * Any lines not containing at least 3 separators (commas or vertical
 * bars) are not considered complete.  If the first 4 fields are
 * empty, they will be stored as empty strings, whereas any other
 * empty fields will be set to NULL.
 *
 * If the separators are commas (,) the component inclination is assumed
 * to be in the SAC convention.  If the separators are vertical bars
 * (|) the component inclination is assumed to be a SEED dip.
 *
 * The SAC convention inclination (degrees down from vertical
 * up/outward) will be converted to SEED dip convention (degrees down
 * from horizontal).
 *
 * Returns number of fields parsed on success and -1 on failure.
 ***************************************************************************/
static int
addmetadata (char *metaline)
{
  struct metanode mn;
  char *lineptr;
  char *fp;
  char *endptr;
  char delim;
  int sacinc = 1;
  int fields = 0;
  int commas = 0;
  int bars = 0;
  int idx;

  if (!metaline)
    return -1;

  /* Count the number of commas */
  fp = metaline;
  while ((fp = strchr (fp, ',')))
  {
    commas++;
    fp++;
  }
  /* Count the number of vertical bars */
  fp = metaline;
  while ((fp = strchr (fp, '|')))
  {
    bars++;
    fp++;
  }

  /* Set delimiter, if vertial bars expect "inclination" to be SEED dip convention */
  if (bars > 0)
  {
    delim = '|';
    sacinc = 1;
  }
  else
  {
    delim = ',';
  }

  /* Must have at least 3 separators for Net, Sta, Loc, Chan ... */
  if (((delim == '|') ? bars : commas) < 3)
  {
    if (verbose > 1)
      fprintf (stderr, "Skipping metadata line: %s\n", metaline);

    return 0;
  }

  /* Create a copy of the line */
  lineptr = strdup (metaline);

  mn.metafields[0] = fp = lineptr;
  mn.starttime = HPTERROR;
  mn.endtime = HPTERROR;

  /* Separate line on delimiter and index in metafields array */
  for (idx = 1; idx < MAXMETAFIELDS; idx++)
  {
    mn.metafields[idx] = NULL;

    if (fp)
    {
      if ((fp = strchr (fp, delim)))
      {
        *fp++ = '\0';

        if (idx <= 3)
           mn.metafields[idx] = fp;

        else if (*fp != delim && *fp != '\0')
          mn.metafields[idx] = fp;

        fields++;
      }
    }
  }

  /* Trim last field if more fields exist */
  if (fp && (fp = strchr (fp, ',')))
    *fp = '\0';

  /* Convert dash-dash location codes to empty strings */
  if (!strcmp (mn.metafields[2], "--"))
  {
    mn.metafields[2] = "";
  }

  /* Parse and convert the value to a dip as needed */
  if (mn.metafields[9])
  {
    mn.dip = strtod (mn.metafields[9], &endptr);
    if (mn.dip == 0.0 && endptr == mn.metafields[9])
    {
      fprintf (stderr, "Error parsing dip: '%s'\n", mn.metafields[9]);
      exit (1);
    }

    /* Convert SAC inclination to SEED dip */
    if (sacinc)
    {
      mn.dip -= 90;
    }
  }

  /* Parse scale factor */
  if (mn.metafields[11])
  {
    mn.scalefactor = strtod (mn.metafields[11], &endptr);
    if (mn.scalefactor == 0.0 && endptr == mn.metafields[11])
    {
      fprintf (stderr, "Error parsing scale factor: '%s'\n", mn.metafields[11]);
      exit (1);
    }
  }

  /* Parse and convert start time */
  if (mn.metafields[15])
  {
    if ((mn.starttime = ms_timestr2hptime (mn.metafields[15])) == HPTERROR)
    {
      fprintf (stderr, "Error parsing metadata start time: '%s'\n", mn.metafields[15]);
      exit (1);
    }
  }

  /* Parse and convert end time */
  if (mn.metafields[16])
  {
    if ((mn.endtime = ms_timestr2hptime (mn.metafields[16])) == HPTERROR)
    {
      fprintf (stderr, "Error parsing metadata end time: '%s'\n", mn.metafields[16]);
      exit (1);
    }
  }

  /* Add the metanode to the metadata list */
  if (!addnode (&metadata, NULL, 0, &mn, sizeof (struct metanode)))
  {
    fprintf (stderr, "Error adding metadata fields to list\n");
  }

  return fields;
} /* End of addmetadata() */


/***************************************************************************
 * readmetadata:
 *
 * Read a file of metadata lines and add to a structured list.  Each
 * line is processed by addmetadata() and should be in the format
 * expected by that routine.
 *
 * Any lines beginning with '#' are skipped, think comments.
 *
 * Returns 0 on sucess and -1 on failure.
 ***************************************************************************/
static int
readmetadata (char *metafile)
{
  FILE *mfp;
  char line[1024];
  char *fp;
  int linecount = 0;

  if (!metafile)
    return -1;

  if ((mfp = fopen (metafile, "rb")) == NULL)
  {
    fprintf (stderr, "Cannot open metadata output file: %s (%s)\n",
             metafile, strerror (errno));
    return -1;
  }

  if (verbose)
    fprintf (stderr, "Reading station/channel metadata from %s\n", metafile);

  while (fgets (line, sizeof (line), mfp))
  {
    linecount++;

    /* Truncate at line return if any */
    if ((fp = strchr (line, '\n')))
      *fp = '\0';

    /* Check for comment line beginning with '#' */
    if (line[0] == '#')
    {
      if (verbose > 1)
        fprintf (stderr, "Skipping comment line: %s\n", line);
      continue;
    }

    if ( addmetadata(line) < 0 )
    {
      fprintf (stderr, "Error adding metadata fields to list for line %d:\n%s\n", linecount, line);
    }
  }

  fclose (mfp);

  return 0;
} /* End of readmetadata() */


/***************************************************************************
 * addnode:
 *
 * Add node to the specified list.
 *
 * Return a pointer to the added node on success and NULL on error.
 ***************************************************************************/
static struct listnode *
addnode (struct listnode **listroot, void *key, int keylen,
	 void *data, int datalen)
{
  struct listnode *lastlp, *newlp;

  if ( data == NULL )
  {
    fprintf (stderr, "addnode(): No data specified\n");
    return NULL;
  }

  lastlp = *listroot;
  while ( lastlp != 0 )
  {
    if ( lastlp->next == 0 )
      break;

    lastlp = lastlp->next;
  }

  /* Create new listnode */
  newlp = (struct listnode *) malloc (sizeof (struct listnode));
  memset (newlp, 0, sizeof (struct listnode));

  if ( key )
  {
    newlp->key = malloc (keylen);
    memcpy (newlp->key, key, keylen);
  }

  if ( data)
  {
    newlp->data = malloc (datalen);
    memcpy (newlp->data, data, datalen);
  }

  newlp->next = 0;

  if ( lastlp == 0 )
    *listroot = newlp;
  else
    lastlp->next = newlp;

  return newlp;
}  /* End of addnode() */


/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version: %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Convert miniSEED data to ASCII (Simple text or GeoCSV)\n\n");
  fprintf (stderr, "Usage: %s [options] input1.mseed [input2.mseed ...]\n\n", PACKAGE);
  fprintf (stderr,
	   " ## Options ##\n"
	   " -V           Report program version\n"
	   " -h           Show this usage message\n"
	   " -v           Be more verbose, multiple flags can be used\n"
	   " -dr          Use the sampling rate derived from the time stamps instead\n"
	   "                of the sample rate denoted in the input data\n"
	   " -i           Process each input file individually instead of merged\n"
           "\n"
	   " -G           Produce GeoCSV formatted output\n"
	   " -c cols      Number of columns for sample value list output (default is %d)\n"
	   " -u units     Specify units string for headers, default is 'Counts'\n"
           " -m metafile    File containing channel metadata (coordinates and more)\n"
           " -M metaline    Channel metadata, same format as lines in metafile\n"
           " -s           Scale data, inversely, by the scale factor in metadata\n"
	   " -f format    Specify output format (default is 1):\n"
           "                1=Header followed by sample value list\n"
           "                2=Header followed by time-sample value pairs\n"
           " -o outfile   Specify the output file, default is segment files\n"
           "\n"
           " -r bytes     Specify SEED record length in bytes, default: autodetect\n"
           " -tt secs     Specify a time tolerance for continuous traces\n"
           " -rt diff     Specify a sample rate tolerance for continuous traces\n",
	   slistcols);

#ifndef NOFDZIP
  fprintf (stderr,
           " -z zipfile   Write all files to a ZIP archive, use '-' for stdout\n"
           " -z0 zipfile  Same as -z but do not compress archive entries\n");
#endif

  fprintf (stderr,
           "\n"
	   "A separate output file is written for each continuous input time-series\n"
	   "with file names of the form:\n"
	   "Net.Sta.Loc.Chan.Qual.YYYY-MM-DDTHHMMSS.FFFFFF.[txt | csv]\n"
	   "\n");
}  /* End of usage() */
