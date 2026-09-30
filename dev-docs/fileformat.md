<!--
    SPDX-FileCopyrightText: 2021-2022 Julius Künzel <julius.kuenzel@kde.org>
    SPDX-FileCopyrightText: 2015-2023 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-License-Identifier: CC0-1.0
-->

# KoraCut File Format

KoraCut's project files (`.koracut` files, and the `.kdenlive` files of older projects, which still open) use an XML format, based on MLT's format (see [MLT’s XML documentation][mlt-xml-doc] and [MLT’s DTD/document type definition][mlt-xml-dtd]) to describe the source media used in a project, as well as the use of that media as in the timeline.

For most media, such as video, audio, and images, KoraCut stores only a reference in a project, but not the media itself. Only some media gets stored directly inside KoraCut’s project files, most notably KoraCut title and color clips.

Important aspects of this file format decision are:

* **MLT is able to directly render KoraCut project files.** MLT simply ignores all the additional KoraCut-specific project data and just sticks to its rendering information. The KoraCut-specific data is the additional icing on top that makes working with projects much easier than editing at the (lower) rendering level.
* **KoraCut can directly include and work with MLT rendering files,** just the same way it works with other media. In fact, KoraCut’s library clips are simply MLT rendering files, and nothing more.

## History and Generations

There are different file format generations depending on the version of KoraCut you're using.

### Generation 1

Generation 1 (gen-1) projects were created and edited the last time using KoraCut up to 0.9.10. These are the KoraCut versions developed for KDE 4.x. (While this isn’t strictly true, we won’t go into discussions about the early KDE 3.x versions of KoraCut.)

The gen-1 projects contain a lot of project data that needs to be duplicated into the outer MLT XML data in order to MLT understanding what it needs to do. As an unfortunate consequence of this data duplicity, gen-1 projects had the habit of getting the outer MLT data out of sync with the inner KoraCut project data. For instance, sometimes the effects as rendered got out of sync with the effect parameters as set in the KoraCut user interface. As the proper remedy for this situation, the KoraCut developers switched to Generation 2 as part of their porting KoraCut to the KDE Frameworks 5 (KF5).

### Generation 2: KF5

*Used in KoraCut versions 15.04 to 17.08*

With the KF5/Qt5 version of KoraCut, we now store all KoraCut data through MLT's xml module to remove the data duplicity from the KoraCut project file structure. This fixed project data model results in much more stable KoraCut project behavior. As another positive side effect, it’s now much easier to manually edit or create KoraCut XML project files from outside KoraCut.

#### Slightly Invalid XML?

When you need to process KoraCut project files using XML tools, there’s a gotcha to be aware in case of gen-2 project files. As soon as a project makes use of track-wide effects, the project XML as written by KoraCut becomes invalid. You won’t notice this when editing KoraCut project files just inside KoraCut, and render them using MLT. You will only notice when editing them using standard XML tools.

The reason is that gen-2 project files used their own attributes inside a separate “kdenlive” namespace. Unfortunately, KoraCut forgets to declare this additional namespace it uses at the beginning of every XML project document. As a simple fix, you’ll need to add the missing namespace yourself before processing the KoraCut project file with XML tools:

```xml
<?xml version='1.0' encoding='utf-8'?>
<mlt xmlns:kdenlive="http://www.kdenlive.org/project" ...>
  ...
</mlt>
```

### Generation 3: Timeline 2

*Used in KoraCut versions 19.04.0 to 20.04.3*

Generation 3 projects are those projects created or edited using KoraCut versions with the new Timeline 2 engine.

### Generation 4: Comma / Point

*Used in KoraCut versions 20.08.0 to 22.12.3, KoraCut document version: 1.00*

With version 20.08.0 a major refactoring of the project file fixed a long standing [issue with the decimal separator][comma-point-issue] (comma/point) conflict causing many crashes. Projects created with 20.08 forward are not backwards compatible.

some minor updates were done to support new features, like:

* 20.12.0: Mixes (in-track-transitions) were introduced

### Generation 5: Current format

*Used in KoraCut versions from 23.04.0, KoraCut document version: 1.1*

An important change was introduced with KoraCut 23.04.0: multiple sequences. Each timeline sequence clip is embedded in an MLT *tractor*.
The "main_bin" playlist lists all clips and timeline sequences used in the project as *entries*.

The very last *tractor* in the project file is just a wrapper for the last opened timeline sequence, so that when playing the project file through the *melt* command line, it will play the last active sequence clip.

Previously, all timeline properties were stored in the "main_bin" playlist as *kdenlive:documentproperty.*. With the new format, all timeline properties are stored inside their timeline sequence as *kdenlive:sequenceproperty.*, and only global project settings are stored in the "main_bin".

## Project XML Structure

The overall structure of the XML data inside KoraCut project files is roughly as illustrated next:

```xml
<mlt producer="main_bin" ...>

  <!-- the profile - definition of the project profile (frame size, aspect ration, etc) -->
  <profile frame_rate_num="25" .../>

  <!-- definition of master and timeline clips used in the first sequence clips -->
  <producer id="producer0" .../>
  <producer id="producer1" .../>

  <!-- then comes the playlist definition - a list of the empty spaces and clips on a track -->
  <playlist id="playlist0">
    <!-- entries linking to the producers defined above -->
    <entry producer="producer0" in="00:00:00.000" out="00:00:04.960">
      <property name="kdenlive:id">3</property>
    </entry>
    <blank length="00:00:03.720"/> <!-- space between clips -->
    <entry producer="producer1" />
    ...
  <playlist id="playlist1"/>

  <!-- After that we have the tracks embedded in a tractor. Each KoraCut timeline track is made of 2 *tracks* to allow mixes-->
  <tractor id="tractor0" in="00:00:00.000" out="00:02:20.840">
    <property name="kdenlive:..">...</property>
    ...
    <track hide="audio" producer="playlist0"/> <!-- hide audio, because this is a video track -->
    <track hide="audio" producer="playlist1"/>
  </tractor>

  <!-- After the tracks definition, we have the first timeline sequence tractor that has a kdenlive:uuid attribute, and transitions between the tracks -->
  <tractor id="tractor6" ...>
    <property name="kdenlive:uuid">...</property>
    <track hide="audio" producer="tractor0"/>
    <track hide="audio" producer="tractor1"/>

    <!-- all transitions -->
    <transition id="transition0"/> <!-- user transitions -->
    <transition id="transition1"> <!-- internally added transition (for example audio mix) -->
      <property name="internal_added">237</property>
    </transition>

    </tractor>

  <!-- If we have multiple timeline sequence clips, we repeat the above steps -->
  <producer id="producer3" .../>
  <playlist id="playlist3" ../>
  <tractor id="tractor7" .../>
  <tractor id="tractor11" ...>
    <property name="kdenlive:uuid">...</property>
    <track hide="audio" producer="tractor7"/>
    <track hide="audio" producer="tractor8"/>
  </tractor>

  <!-- The *main bin* playlist, keeping all project settings, and a list of all project bin clips, including the sequence clips  -->
  <playlist id="main_bin">
    <property name="kdenlive:documentproperty...">...</property>
    <entry producer="producer0" .../>
    <entry producer="producer1" .../>
    <entry producer="tractor6" .../>
    <entry producer="tractor11" .../>
  </playlist>

  <!-- the last tractor of the xml file only contains one producerm is a the main tractor, containing the default sequence clip that will be played -->
  <tractor id="tractor23" in="00:00:00.000" out="00:00:45.000">
    <property name="kdenlive:projectTractor">1</property>
    <track producer="tractor11" in="00:00:00.000" out="00:00:45.000"/>
 </tractor>

</mlt>
```

We avoid storing any inner KoraCut project data that’s already present in the outer MLT data layer, this means that all information must be stored in MLT objects like Tractor, Playlist, Producer, etc.

To separate these properties from other MLT properties, we prefix them with `kdenlive:`. This page lists the properties that we use in this new file format.

Properties applied to project clips (MLT Producer object):

| Name                 | Description |
| -------------------- | ----------- |
| kdenlive:clipname    | Stores the name that will be displayed for this clip in the Project Bin. |
| kdenlive:folderid    | Stores a string containing the id of the folder where this clip is (empty if clip is in the root folder). |
| kdenlive:zone_in     | Stores the "in" point for the play zone defined for this clip. |
| kdenlive:zone_out    | Stores the "out" point for the play zone defined for this clip. |
| kdenlive:originalurl | Stores the clip's original url. Useful to retrieve original url when a clip was proxied. |
| kdenlive:proxy       | Stores the url for the proxy clip, or "-" if no proxy should be used for this clip. |
| kdenlive:clip_type   |  |
| kdenlive:id          |  |
| kdenlive:file_size   |  |
| kdenlive:file_hash   |  |
| kdenlive:audio_max1  |  |


### Project Bin

The (project) bin is represented by an MLT `<playlist>` element with the well-known id “main_bin”. This element holds project-specific (meta) data, the bin folders as well as their hierarchy, clip groups, and some more stuff.

> **Note**
>
> While KoraCut now mainly uses the term “bin” when talking about what formerly was the “project bin”, the early internal name “name bin” has stuck since the early days of KoraCut.

Properties applied to the Bin Playlist (MLT Playlist object "main_bin"):

| Name                     | Description |
| ------------------------ | ----------- |
| kdenlive:folder.xxx.yyy  | This property stores the names of folders created in the Project Bin. xxx is the id of the parent folder (-1 for root) and yyy is the id for this folder. The value of this property is the name of the folder. |
| kdenlive:docproperties.* | There are various properties with this prefix holding settings related to the current project, like proxy size |
| kdenlive:documentnotes   |  |
| kdenlive:docproperties.groups | This property holds a json structure representing the clip groups of the timeline |


### Timeline Tracks

The setup of the timeline tracks is represented by an MLT `<tractor>` element, this “maintractor“ can be identified by being the last `<tractor>` in the file. The elements inside are the individual timeline tracks: these are referenced, with the actual tracks then being MLT producers.

The individual KoraCut timeline tracks can be found in form of MLT `<tractor>` elements holding `<playlist>` elements. They can also easily be identified according to their “tractor#”/“playlist#” id naming scheme; here, “#” is a number internally maintained by KoraCut. The tracks feature additional properties that describe their title, locking state, and some more. The reason for the extra level with `<playlist>` elements are mixes (see below).

There is one semi-internal track here, the built-in “black_track”. As you can see here, while it is built into KoraCut, it isn’t so into MLT. Instead, to MLT this is just another track that happen to be created by KoraCut. KoraCut never shows this background track in the timeline as an individual track. But you can reference it from transitions (the composite ones, that is).

### Subtitle Track

While for the user it feels like this subtitle track is a track like an audio or video track, in fact it is not. It is a "fake" track and internally a filter, the [`avfilter.subtitles`][subtitle-mlt-doc] filter to be more precise (see also [this ffmpeg doc][subtile-ffmpeg-doc]). If an internal `avfilter.subtitles` is detected, the subtitle track gets enabled.

The subtitles are stored in a `*.srt` file next to the project. If your project is named `mymovie.koracut`, the subtitle file will be `mymovie.koracut.srt`. This file will only be update if you save your project, but the internal subtitle model is not sufficient during work since `avfilter.subtitles` needs a `*.srt` file to always show the up-to-date state of work in the monitor. Therefore there is another `*.srt` file in the temp directory (`/tmp` on Linux).

This is how the subtitle track is represented in the project XML:

```xml
  <filter id="filter9">
   <property name="mlt_service">avfilter.subtitles</property>
   <property name="internal_added">237</property>
   <property name="av.filename">/tmp/1654509160006.srt</property>
   <property name="kdenlive:locked">1</property>
  </filter>
```

### Transitions (Compositions)

Transitions come in two (three) flavors, but always represented by MLT `<transition>` elements:

* user transitions are the ones added by, you’ve guessed it … users. These are the transitions currently shown slightly moved down and overlaying timeline tracks and their clips.
* internally added transitions are basically a convenience to make the timeline do automatic audio mixing across multiple tracks, and (with more recent KoraCut versions) transparent tracks.

### Same-Track-Transitions (Mixes)

TODO

### Filters (Effect)

TODO


### Not Shown

* bin clip producers
* proxy clips

## Further Reading

* The [KoraCut Project Analyzer][project-analyizer-blog] is kind of a (crazy) simple KoraCut doctor, showing some statistics and details when given a KoraCut gen-2 project file. You can even try the KoraCut Project Analyzer [online][project-analyizer-tool] using Chrome/Chromium or Firefox. [Sources are available on GitHub.][project-analyizer-code]

[mlt-xml-doc]: https://www.mltframework.org/docs/mltxml/
[mlt-xml-dtd]: https://github.com/mltframework/mlt/blob/master/src/modules/xml/mlt-xml.dtd
[subtitle-mlt-doc]: https://www.mltframework.org/plugins/FilterAvfilter-subtitles/
[subtile-ffmpeg-doc]: https://ffmpeg.org/ffmpeg-filters.html#subtitles-1
[comma-point-issue]: https://invent.kde.org/multimedia/kdenlive/-/issues/78
[project-analyizer-blog]: https://thediveo-e.blogspot.de/2016/07/inside-kdenlive-projects-analyzer.html
[project-analyizer-tool]: https://thediveo.github.io/kdenlive-project-analyzer/kdenlive-project-analyzer.html
[project-analyizer-code]: https://github.com/TheDiveO/kdenlive-project-analyzer


