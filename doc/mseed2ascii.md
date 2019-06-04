# <p >mseed2ascii 
###  miniSEED to ASCII converter</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Metadata Files](#metadata-files)
1. [List Files](#list-files)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
mseed2ascii [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>mseed2ascii</b> converts miniSEED time-series data to ASCII formats, either simple text or GeoCSV.  If an input file name is prefixed with an '@' character the file is assumed to contain a list of input data files, see <i>LIST FILES</i> below.</p>

<p >A separate output file is written for each continuous time-series in the input data.  Output file names are of the form:</p>

<pre >
"Net.Sta.Loc.Chan.Qual.YYYY-MM-DDTHHMMSS.FFFFFF.txt"
or
"Net.Sta.Loc.Chan.Qual.YYYY-MM-DDTHHMMSS.FFFFFF.csv"
</pre>

<pre >
For example:
"TA.ELFS..LHZ.R.2006-04-22T153619.000000.txt"
</pre>

<p >If the input file name is "-" input miniSEED records will be read from standard input.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-dr</b>

<p style="padding-left: 30px;">Use the sampling rate derived from the start and end times and the number of samples instead of the rate specified in the input data. This is useful when the sample rate in the input data does not have enough resolution to represent the true rate.</p>

<b>-i</b>

<p style="padding-left: 30px;">Process each input file individually.  By default all input files are read and all data is buffered in memory before ASCII files are written. This allows time-series spanning mutilple input files to be merged and written in a single ASCII file.  The intention is to use this option when processing large amounts of data in order to keep memory usage within reasonable limits.</p>

<b>-G</b>

<p style="padding-left: 30px;">Produce GeoCSV formatted output. With this option multi-column sample lists are not allowed, i.e. the <b>-c</b> option is ignored.</p>

<b>-E </b><i>key:value</i>

<p style="padding-left: 30px;">Insert extra header with <b>key</b> and <b>value</b> into GeoCSV formatted output.  This option may be specified multiple times.</p>

<b>-c </b><i>cols</i>

<p style="padding-left: 30px;">Specify the number of columns to use for sample list formatted output, default is 1 column.</p>

<b>-u </b><i>units</i>

<p style="padding-left: 30px;">Specify the units string that should be included in the ASCII output headers, the default is "Counts".</p>

<b>-m </b><i>metafile</i>

<p style="padding-left: 30px;">Specify a file containing metadata such as coordinates, elevation, component orientation, scaling factor, etc.  For each time-series written any matching metadata will be added to the GeoCSV header.  See <i>METADATA FILES</i> below.</p>

<b>-M </b><i>metaline</i>

<p style="padding-left: 30px;">Specify a "line" of metadata in the same format as expected for the <i>METADATA FILES</i>.  This option may be specified multiple times.</p>

<b>-s</b>

<p style="padding-left: 30px;">Scale the data, inversely, by the scale factor in metadata.  Integer data will be converted to float type.  The relevant units from the metadata will be reported in the header.</p>

<b>-f </b><i>format</i>

<p style="padding-left: 30px;">The default output format is sample list.  This option applies to all output files:</p>

<pre style="padding-left: 30px;">
1 : Sample list format, header includes time stamp of first
2 : Time-sample pair format, each sample value listed with time stamp
</pre>

<b>-o </b><i>outfile</i>

<p style="padding-left: 30px;">Write all ASCII output to <i>outfile</i>, if <i>outfile</i> is a single dash (-) then all output will go to stdout.  If this option is not specified each contiguous segment is written to a separate file.  All diagnostic output from the program is written to stderr and should never get mixed with data going to stdout.</p>

<b>-z </b><i>zipfile</i>

<p style="padding-left: 30px;">Create a ZIP archive containing all output files instead of writing individual files.  Each file is compressed with the deflate method. Specify <b>"-"</b> (dash) to write ZIP archive to stdout.</p>

<b>-z0 </b><i>zipfile</i>

<p style="padding-left: 30px;">Same as <i>"-z"</i> except do not compress the output files.  Specify <b>"-"</b> (dash) to write ZIP archive to stdout.</p>

<b>-r </b><i>bytes</i>

<p style="padding-left: 30px;">Specify the miniSEED record length in <i>bytes</i>, by default this is autodetected.</p>

<b>-tt </b><i>secs</i>

<p style="padding-left: 30px;">Specify a time tolerance for constructing continous trace segments. The tolerance is specified in seconds.  The default tolerance is 1/2 of the sample period.</p>

<b>-rt </b><i>diff</i>

<p style="padding-left: 30px;">Specify a sample rate tolerance for constructing continous trace segments.  The tolerance is specified as the difference between two sampling rates.  The default tolerance is tested as: (abs(1-sr1/sr2) < 0.0001).</p>

## <a id='metadata-files'>Metadata Files</a>

<p >A metadata file contains a list of station parameters, some of which can be stored in GeoCSV but not in miniSEED.  Each line in a metadata file should be a list of parameters in the order shown below.  Each parameter should be separated with a comma (,) or a vertical bar (|).</p>

<p ><b>DIP CONVENTION:</b> When vertical bars are used the dip field is assumed to be in the SEED convention (degrees down from horizontal), if comma separators are used the dip field (CMPINC) is assumed to be in the SAC convention (degrees down from vertical up/outward) and converted to SEED convention.</p>

<p ><b>Metdata fields</b>:</p>
<pre >
Network
Station
Location
Channel
Latitude
Longitude
Elevation, in meters
Depth, in meters
Component Azimuth, degrees clockwise from north
Component Dip, degrees from horizontal
Instrument Name
Scale Factor
Scale Frequency
Scale Units
Sampling rate
Start time, used for matching
End time, used for matching

Example with vertical bar separators (with SEED convention dip):

------------------
#net|sta|loc|chan|lat|lon|elev|depth|azimuth|SEEDdip|instrument|scale|scalefreq|scaleunits|samplerate|start|end
IU|ANMO|00|BH1|34.945981|-106.457133|1671|145|328|0|Geotech KS-54000|3456610000|0.02|M/S|20|2008-06-30T20:00:00|2599-12-31T23:59:59
IU|ANMO|00|BH2|34.945981|-106.457133|1671|145|58|0|Geotech KS-54000|3344370000|0.02|M/S|20|2008-06-30T20:00:00|2599-12-31T23:59:59
IU|ANMO|00|BHZ|34.945981|-106.457133|1671|145|0|-90|Geotech KS-54000|3275080000|0.02|M/S|20|2008-06-30T20:00:00|2599-12-31T23:59:59
------------------

As a special case '--' can be used to match an empty location code.
</pre>

<p >For each time-series written, metadata from the first line with matching source name parameters (network, station, location and channel) and time window (if specified) will be inserted into the GeoCSV headers.  All parameters are optional except for the first four fields specifying the source name parameters.</p>

## <a id='list-files'>List Files</a>

<p >If an input file is prefixed with an '@' character the file is assumed to contain a list of file for input.  Multiple list files can be combined with multiple input files on the command line.  The last, space separated field on each line is assumed to be the file name to be read.</p>

<p >An example of a simple text list:</p>

<pre >
TA.ELFS..LHE.R.mseed
TA.ELFS..LHN.R.mseed
TA.ELFS..LHZ.R.mseed
</pre>

## <a id='author'>Author</a>

<pre >
Chad Trabant
IRIS Data Management Center
</pre>


(man page 2018/03/15)
