/*
   Disk Info: an Amiga track analyser.

   Two circular surfaces, one per side, drawn from what the drive actually found on
   each track rather than from what a formatter would have put there. The firmware
   sends four messages per track - a census, a surface profile, a flux histogram -
   and this draws them; every judgement about whether a sector is good was made on
   the Teensy, by the same code that judges a real disk read.

   The same messages arrive whether the source was the drive or an SCP image off
   the memory card, which is deliberate: there is one analysis path in the firmware
   and so there is one renderer here.
*/

// Sector verdicts. These are XCopySectorVerdict in XCopyFloppy.h and travel as
// numbers, so they may not be renumbered on one side only.
const DINFO_MISSING = 0;
const DINFO_OK = 1;
const DINFO_CYL_LOW = 2;
const DINFO_CYL_HIGH = 3;
const DINFO_HEAD_WRONG = 4;
const DINFO_BAD_CHECK = 5;

// Sector number carried by a sync mark whose header could not be trusted.
const DINFO_STRAY = 255;

// Flags in the dinfoTrack message, DINFO_* in XCopyDiskInfo.h.
const DINFO_ALIGNED = 0x01;
const DINFO_HD = 0x02;
const DINFO_FROMFILE = 0x04;

const DINFO_MAX_CYLINDERS = 84;
// FLOPPY_MAX_SYNCS in XCopyFloppy.h: sync marks one capture can hold, and so the
// point past which a track's census is known to be incomplete.
const DINFO_MAX_SYNCS = 64;
const DINFO_BUCKETS = 512;
const DINFO_HIST_BINS = 64;

// Canvas geometry. The annulus is 174px for 84 cylinders, so a ring is about two
// pixels: that is the reason the firmware sends 512 angular buckets and not more.
const DINFO_SIZE = 520;
const DINFO_CX = 260;
const DINFO_CY = 260;
const DINFO_R_OUTER = 240;
const DINFO_R_HUB = 66;
const DINFO_RING = (DINFO_R_OUTER - DINFO_R_HUB) / DINFO_MAX_CYLINDERS;

const TAU = Math.PI * 2;

/*
   The model. Tracks and profiles are keyed by logical track, cylinder * 2 + side,
   the same numbering the firmware and the rest of this UI use.
*/
var dinfoTracks = {};
var dinfoProfiles = {};
var dinfoHists = {};
var dinfoMeta = null;
var dinfoSelected = { cylinder: 0, side: 0 };

/*
   Decoded sectors, keyed "cylinder:side:sector".

   The firmware keeps no sector data once a survey has moved on - 168 tracks of it
   is far past what the part has - so each one is fetched on demand and kept here.
   A value of "pending" is a request already in flight, which is what stops a
   pointer sliding along a track from asking for the same sector fifty times.
*/
var dinfoSectorCache = {};
var dinfoHoverKey = null;

// The painted surface is cached per side. Repainting it is a quarter of a million
// pixels of trigonometry; selecting a track must not pay that, so the speckle goes
// to an offscreen canvas and every redraw is a drawImage plus the overlays.
var dinfoSurface = [null, null];
var dinfoSurfaceDirty = [true, true];

// ---------------------------------------------------------------------------
// commands out
// ---------------------------------------------------------------------------

function dinfoRange() {
  var first = parseInt($('#dinfoFirst').val(), 10);
  var last = parseInt($('#dinfoLast').val(), 10);
  var side = parseInt($('#dinfoSide').val(), 10);

  if (isNaN(first) || first < 0) { first = 0; }
  if (isNaN(last) || last > DINFO_MAX_CYLINDERS - 1) { last = DINFO_MAX_CYLINDERS - 1; }
  if (last < first) { last = first; }
  if (isNaN(side)) { side = -1; }

  return first + "-" + last + "," + side;
}

function diskInfoScan() {
  wsSend("diskInfoScan," + dinfoRange());
}

// Called by the shared file-select modal, which is put into SCP mode by the
// Load SCP button; see FILESELECT_MODES in sdcard.js.
function diskInfoFile(path) {
  $('#staticBackdrop').modal('hide');
  wsSend("diskInfoFile," + dinfoRange() + "," + path);
}

// ---------------------------------------------------------------------------
// messages in
// ---------------------------------------------------------------------------

function dinfoBegin(source, firstCyl, lastCyl, side, hd, wrprot, rotMs, name) {
  dinfoTracks = {};
  dinfoProfiles = {};
  dinfoHists = {};
  // The disk in the drive may not be the one these came from.
  dinfoSectorCache = {};
  dinfoHoverKey = null;
  dinfoSurfaceDirty = [true, true];

  dinfoMeta = {
    source: source,
    name: name,
    firstCyl: parseInt(firstCyl, 10),
    lastCyl: parseInt(lastCyl, 10),
    side: parseInt(side, 10),
    hd: hd == "1",
    wrprot: wrprot == "1",
    rotMs: parseInt(rotMs, 10),
    sectors: 0,
    bad: 0,
    missing: 0,
    complete: false
  };

  dinfoSelected = { cylinder: parseInt(firstCyl, 10), side: 0 };
  dinfoDrawAll();
}

/*
   One track's census.

   `sectors` is a | separated list of one entry per sync mark the capture found, in
   the order it found them: "<sectorNumber>:<verdict>:<position>". Position is
   0..4095 of a revolution, so it is an angle, and it is what puts a sector mark
   where it physically sits rather than where a formatter would have put it.
*/
function dinfoTrack(cyl, side, syncs, valid, bad, strays, dups, trunc, over, cylSeen, cells, revBytes, revUs, cellPs, sectorCount, flags, sectors) {
  cyl = parseInt(cyl, 10);
  side = parseInt(side, 10);

  var list = [];
  if (sectors !== undefined && sectors !== "") {
    sectors.split("|").forEach(function (entry) {
      var f = entry.split(":");
      if (f.length == 3) {
        list.push({ sector: parseInt(f[0], 10), verdict: parseInt(f[1], 10), pos: parseInt(f[2], 10) });
      }
    });
  }

  dinfoTracks[cyl * 2 + side] = {
    cylinder: cyl,
    side: side,
    syncs: parseInt(syncs, 10),
    valid: parseInt(valid, 10),
    bad: parseInt(bad, 10),
    strays: parseInt(strays, 10),
    dups: parseInt(dups, 10),
    trunc: parseInt(trunc, 10),
    over: parseInt(over, 10),
    cylSeen: parseInt(cylSeen, 10),
    cells: parseInt(cells, 10),
    revBytes: parseInt(revBytes, 10),
    revUs: parseInt(revUs, 10),
    cellPs: parseInt(cellPs, 10),
    sectorCount: parseInt(sectorCount, 10),
    flags: parseInt(flags, 10),
    sectors: list
  };

  dinfoSurfaceDirty[side] = true;
  dinfoDrawSide(side);
  if (cyl == dinfoSelected.cylinder && side == dinfoSelected.side) { dinfoDrawDetail(); }
}

/*
   One track's surface, DINFO_BUCKETS density levels as hex nibbles.

   Unpacked once here rather than on every repaint: the string is 512 characters
   and the surface is redrawn whenever a track lands.
*/
function dinfoProfile(cyl, side, nibbles) {
  cyl = parseInt(cyl, 10);
  side = parseInt(side, 10);

  var levels = new Uint8Array(DINFO_BUCKETS);
  var n = Math.min(DINFO_BUCKETS, nibbles.length);
  for (var i = 0; i < n; i++) {
    levels[i] = parseInt(nibbles.charAt(i), 16) || 0;
  }

  dinfoProfiles[cyl * 2 + side] = levels;
  dinfoSurfaceDirty[side] = true;
  dinfoDrawSide(side);
}

//! One track's flux interval histogram, DINFO_HIST_BINS bytes as hex pairs.
function dinfoHist(cyl, side, hex) {
  cyl = parseInt(cyl, 10);
  side = parseInt(side, 10);

  var bins = new Uint8Array(DINFO_HIST_BINS);
  for (var i = 0; i < DINFO_HIST_BINS && (i * 2 + 1) < hex.length; i++) {
    bins[i] = parseInt(hex.substr(i * 2, 2), 16) || 0;
  }

  dinfoHists[cyl * 2 + side] = bins;
  if (cyl == dinfoSelected.cylinder && side == dinfoSelected.side) { dinfoDrawDetail(); }
}

/*
   One decoded sector, in reply to a hover.

   Held whether or not it decoded: a sector the firmware could not find is still an
   answer, and caching it stops the same miss being asked for again every time the
   pointer crosses it.
*/
function dinfoSector(cyl, side, sector, found, format, trackId, toGap,
                     hdrSum, hdrCalc, dataSum, dataCalc, startCell, dataCell, endCell, hex) {
  var key = parseInt(cyl, 10) + ":" + parseInt(side, 10) + ":" + parseInt(sector, 10);

  dinfoSectorCache[key] = {
    cylinder: parseInt(cyl, 10),
    side: parseInt(side, 10),
    sector: parseInt(sector, 10),
    found: found == "1",
    format: parseInt(format, 10),
    trackId: parseInt(trackId, 10),
    toGap: parseInt(toGap, 10),
    hdrSum: parseInt(hdrSum, 10),
    hdrCalc: parseInt(hdrCalc, 10),
    dataSum: parseInt(dataSum, 10),
    dataCalc: parseInt(dataCalc, 10),
    startCell: parseInt(startCell, 10),
    dataCell: parseInt(dataCell, 10),
    endCell: parseInt(endCell, 10),
    hex: hex === undefined ? "" : hex
  };

  // Only paint it if the pointer is still on the sector that asked.
  if (dinfoHoverKey === key) { dinfoDrawDump(dinfoSectorCache[key]); }
}

function dinfoEnd(tracks, good, bad, missing) {
  if (dinfoMeta == null) { return; }
  dinfoMeta.sectors = parseInt(good, 10);
  dinfoMeta.bad = parseInt(bad, 10);
  dinfoMeta.missing = parseInt(missing, 10);
  dinfoMeta.tracks = parseInt(tracks, 10);
  dinfoMeta.complete = true;
  dinfoDrawAll();
}

// ---------------------------------------------------------------------------
// colour
// ---------------------------------------------------------------------------

function dinfoVerdictColour(verdict, sector) {
  if (sector == DINFO_STRAY) { return "#ab47bc"; }
  switch (verdict) {
    case DINFO_OK: return "#66bb6a";
    case DINFO_BAD_CHECK: return "#ef5350";
    case DINFO_CYL_LOW:
    case DINFO_CYL_HIGH:
    case DINFO_HEAD_WRONG: return "#ffa726";
    default: return "#757575";
  }
}

/*
   The base colour of a track's surface, which the density then brightens.

   Status rather than a flat texture colour: the point of drawing the whole disk at
   once is to see which parts of it are in trouble without clicking through 168
   tracks, and a surface that is uniformly purple cannot say that. A track with no
   sync marks at all is deliberately its own colour and not "bad" - unformatted and
   unreadable are different things, and on a protected disk the difference matters.
*/
function dinfoTint(track) {
  if (track === undefined) { return [34, 34, 34]; }
  if (track.syncs == 0) { return [46, 48, 78]; }

  /*
     Sync marks alone do not make a track formatted.

     0x4489 is chosen to be illegal in encoded data, which says nothing about a
     surface where nothing is encoded: an unwritten track throws the pattern by
     accident, and keying the colour off track.syncs painted a wiped disk as one
     in trouble from edge to edge. cylSeen is the first header that checksummed
     and named a cylinder in range, so it is the cheapest thing here that noise
     cannot fake - without it there are marks but no format, which is a different
     finding from a format that will not read.
  */
  if (track.cylSeen < 0) { return [64, 56, 96]; }

  if (track.valid >= track.sectorCount && track.sectorCount > 0) { return [42, 92, 58]; }
  if (track.valid == 0) { return [104, 40, 40]; }
  return [116, 88, 34];
}

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------

/*
   Angle of a position on the track, 0 at twelve o'clock.

   The two sides run opposite ways because they are drawn as the disk is actually
   seen: side 0 from underneath and side 1 from above, which is what "bottom view"
   and "top view" mean on the HxC panel this follows. Draw both the same way round
   and the pattern on one of them is a mirror image of where it really is.

   Which way round each one goes is NOT the direction the hub arrows show. Those
   are the rotation - which way the surface travels under the head - and the track
   is laid down against it: the head is fixed while the disk turns beneath it, so
   the material arriving under it a moment later is the material that was sitting a
   little way BACK along the rotation. Time therefore advances round the drawing
   opposite to the arrow, and running it with the arrow instead - which is what
   this did, on both sides - mirrors every track about twelve o'clock and reverses
   the direction the sector pattern appears to spiral in.

   Checked against HxC rather than argued from the physics alone. Measuring the
   angular drift of the sector marks per ring, on the same image in both tools:
   HxC runs side 0 clockwise and side 1 anticlockwise going outward, and this file
   ran side 0 anticlockwise - the mirror of it, on the side with enough marks to
   measure. These signs are what put the two the same way round, so a change here
   is a change away from HxC.
*/
function dinfoAngle(pos, side) {
  var t = pos / 4096;
  return side === 0 ? (-Math.PI / 2 + t * TAU) : (-Math.PI / 2 - t * TAU);
}

function dinfoRadius(cylinder) {
  // Cylinder 0 is the outermost track on the disk, so it is the outermost ring.
  return DINFO_R_OUTER - (cylinder * DINFO_RING);
}

// ---------------------------------------------------------------------------
// the surface
// ---------------------------------------------------------------------------

/*
   Paint one side's magnetic surface into its offscreen cache.

   Done per pixel rather than as 84 x 512 arc fills: forty three thousand fill
   calls a side is slower than one pass over the pixels, and the pixel pass gives
   the texture without seams where the segments meet.
*/
function dinfoPaintSurface(side) {
  if (dinfoSurface[side] == null) {
    dinfoSurface[side] = document.createElement("canvas");
    dinfoSurface[side].width = DINFO_SIZE;
    dinfoSurface[side].height = DINFO_SIZE;
  }

  var ctx = dinfoSurface[side].getContext("2d");
  var img = ctx.createImageData(DINFO_SIZE, DINFO_SIZE);
  var data = img.data;

  var outer2 = DINFO_R_OUTER * DINFO_R_OUTER;
  var hub2 = DINFO_R_HUB * DINFO_R_HUB;

  // Hoisted out of the loop: one lookup per cylinder rather than per pixel.
  var tints = [];
  for (var c = 0; c < DINFO_MAX_CYLINDERS; c++) {
    tints.push(dinfoTint(dinfoTracks[c * 2 + side]));
  }

  for (var y = 0; y < DINFO_SIZE; y++) {
    var dy = y - DINFO_CY;
    for (var x = 0; x < DINFO_SIZE; x++) {
      var dx = x - DINFO_CX;
      var r2 = dx * dx + dy * dy;
      if (r2 > outer2 || r2 < hub2) { continue; }

      var r = Math.sqrt(r2);
      var cyl = Math.floor((DINFO_R_OUTER - r) / DINFO_RING);
      if (cyl < 0 || cyl >= DINFO_MAX_CYLINDERS) { continue; }

      var tint = tints[cyl];
      var level = 0;
      var profile = dinfoProfiles[cyl * 2 + side];
      if (profile !== undefined) {
        // Inverse of dinfoAngle(): screen angle back to a position on the track.
        var a = Math.atan2(dy, dx);
        var t = side === 0 ? (a + Math.PI / 2) : (-Math.PI / 2 - a);
        t /= TAU;
        t -= Math.floor(t);
        level = profile[Math.floor(t * DINFO_BUCKETS) % DINFO_BUCKETS];
      }

      // A track that has been analysed but has no surface yet still shows its
      // tint, so a scan in progress fills in rather than flickering.
      var scale = profile === undefined ? 0.55 : (0.28 + (0.72 * level / 15));
      var o = (y * DINFO_SIZE + x) * 4;
      data[o] = tint[0] * scale;
      data[o + 1] = tint[1] * scale;
      data[o + 2] = tint[2] * scale;
      data[o + 3] = 255;
    }
  }

  ctx.putImageData(img, 0, 0);
  dinfoSurfaceDirty[side] = false;
}

/*
   Everything that sits on top of the surface: the hub, the sector marks and the
   selection. Redrawn on every selection change, which is why it is not baked into
   the cached surface.
*/
function dinfoDrawSide(side) {
  var canvas = document.getElementById("diskCanvas" + side);
  if (canvas == null) { return; }
  var ctx = canvas.getContext("2d");

  ctx.fillStyle = "#050505";
  ctx.fillRect(0, 0, DINFO_SIZE, DINFO_SIZE);

  if (dinfoSurfaceDirty[side]) { dinfoPaintSurface(side); }
  ctx.drawImage(dinfoSurface[side], 0, 0);

  // sector marks, every analysed track
  for (var c = 0; c < DINFO_MAX_CYLINDERS; c++) {
    var track = dinfoTracks[c * 2 + side];
    if (track === undefined) { continue; }

    var rOuter = dinfoRadius(c);
    var rInner = rOuter - DINFO_RING;

    track.sectors.forEach(function (s) {
      var a = dinfoAngle(s.pos, side);
      ctx.beginPath();
      ctx.strokeStyle = dinfoVerdictColour(s.verdict, s.sector);
      ctx.lineWidth = 1;
      ctx.moveTo(DINFO_CX + Math.cos(a) * rInner, DINFO_CY + Math.sin(a) * rInner);
      ctx.lineTo(DINFO_CX + Math.cos(a) * rOuter, DINFO_CY + Math.sin(a) * rOuter);
      ctx.stroke();
    });
  }

  // the selected cylinder
  if (dinfoSelected.cylinder >= 0 && dinfoSelected.cylinder < DINFO_MAX_CYLINDERS) {
    var rs = dinfoRadius(dinfoSelected.cylinder) - (DINFO_RING / 2);
    ctx.beginPath();
    ctx.arc(DINFO_CX, DINFO_CY, rs, 0, TAU);
    ctx.strokeStyle = side == dinfoSelected.side ? "#00e676" : "#00796b";
    ctx.lineWidth = 2;
    ctx.stroke();
  }

  dinfoDrawHub(ctx, side);
}

function dinfoDrawHub(ctx, side) {
  // guard
  ctx.beginPath();
  ctx.arc(DINFO_CX, DINFO_CY, DINFO_R_HUB, 0, TAU);
  ctx.lineWidth = 10;
  ctx.strokeStyle = "#000000";
  ctx.fillStyle = "#303030";
  ctx.fill();
  ctx.stroke();

  // hub
  ctx.beginPath();
  ctx.arc(DINFO_CX, DINFO_CY, DINFO_R_HUB / 1.7, 0, TAU);
  ctx.lineWidth = 2;
  ctx.strokeStyle = "#000000";
  ctx.fillStyle = "#101010";
  ctx.fill();
  ctx.stroke();

  // index hole, at twelve o'clock: the angle everything else is measured from
  ctx.beginPath();
  ctx.arc(DINFO_CX, DINFO_CY - (DINFO_R_HUB * 0.78), 4, 0, TAU);
  ctx.fillStyle = "#e0e0e0";
  ctx.fill();

  ctx.fillStyle = "#9e9e9e";
  ctx.font = "11px monospace";
  ctx.textAlign = "center";
  ctx.fillText(side === 0 ? "Side 0" : "Side 1", DINFO_CX, DINFO_CY - 6);
  ctx.fillText(side === 0 ? "Bottom view" : "Top view", DINFO_CX, DINFO_CY + 8);
  // Which way the surface turns under the head, as seen from this side.
  ctx.fillText(side === 0 ? "<-" : "->", DINFO_CX, DINFO_CY + 24);
  ctx.textAlign = "left";
}

function dinfoDrawAll() {
  dinfoDrawSide(0);
  dinfoDrawSide(1);
  dinfoDrawHeaders();
  dinfoDrawDetail();
}

// ---------------------------------------------------------------------------
// headers and status
// ---------------------------------------------------------------------------

/*
   One side's totals.

   "bad" is sectors that were found and did not read, which is the firmware's
   CalibrationResult::bad and the same thing HxC's panel counts. It is deliberately
   not sectorCount - valid: that counts a track which was never formatted as eleven
   failures, so a wiped disk whose every readable sector was perfect came back as
   836 bad, and the number said more about the assumption than the disk.

   What a standard format would have added on top of what was found is counted
   separately as missing, because it is worth knowing and is not damage.
*/
function dinfoSideSummary(side) {
  var tracks = 0, sectors = 0, bad = 0, missing = 0, bytes = 0, over = 0;
  for (var c = 0; c < DINFO_MAX_CYLINDERS; c++) {
    var t = dinfoTracks[c * 2 + side];
    if (t === undefined) { continue; }
    tracks++;
    sectors += t.valid;
    bad += t.bad;
    missing += Math.max(0, t.sectorCount - t.valid - t.bad);
    bytes += t.valid * 512;
    if (t.over > 0) { over++; }
  }
  return { tracks: tracks, sectors: sectors, bad: bad, missing: missing, bytes: bytes, over: over };
}

function dinfoDrawHeaders() {
  for (var side = 0; side < 2; side++) {
    var s = dinfoSideSummary(side);
    var text = "Side " + side + ", " + s.tracks + " Tracks";
    if (s.tracks > 0) {
      text += "<br/>" + s.sectors + " Sectors, " + s.bad + " bad";
      if (s.missing > 0) { text += "<br/>" + s.missing + " missing"; }
      text += "<br/>" + s.bytes + " Bytes";
      text += "<br/>Amiga MFM";
      if (dinfoMeta != null) {
        text += "<br/>WrProt " + (dinfoMeta.wrprot ? "ON" : "OFF");
      }
      // Only when it happened. A census that ran out of table has described part
      // of those tracks as though it were all of them, and that has to be said.
      if (s.over > 0) { text += "<br/>" + s.over + " tracks over " + DINFO_MAX_SYNCS + " marks"; }
    } else {
      text += "<br/>nothing read";
    }
    $('#dinfoHead' + side).html(text);
    $('#dinfoSource' + side).html(dinfoMeta == null ? "&mdash;" : dinfoMeta.name);
  }
}

/*
   How long the track is, in bit cells.

   From the revolution length the firmware measured, not from `cells` - that field
   is XCopyFloppy::getBitCount(), which counts entries to the capture interrupt and
   so counts flux transitions, gap and noise included. Transitions and cells are
   not the same number and MFM does not give a fixed ratio between them, so the two
   are reported as the separate things they are rather than one being passed off
   as the other.
*/
function dinfoTrackCells(track) {
  if (track === undefined || !track.revBytes) { return null; }
  return track.revBytes * 8;
}

/*
   One revolution of this track, in microseconds.

   Per track, from the firmware. For an image that is the file's own index to index
   time; for a drive it is the rotation measured once at the start of the survey,
   sent on every track so there is one place this is read from.
*/
function dinfoTrackUs(track) {
  if (track !== undefined && track.revUs) { return track.revUs; }
  if (dinfoMeta != null && dinfoMeta.rotMs) { return dinfoMeta.rotMs * 1000; }
  return 0;
}

/*
   Cells per second, which is what "bitrate" means here.

   From the measured cell period and nothing else. This used to be derived as
   cells / milliseconds, and both of those came from the same revolution length
   under the same assumed 2us cell - so the revolution cancelled and the answer was
   exactly 500 for every DD track ever surveyed, whatever was on it. A number that
   cannot come out any other way is not a measurement, and a drive running slow or
   a track written long is precisely what it was being read to find.

   XCopyFloppy::measureCellPeriod() takes it from the flux interval histogram
   instead, so it moves with the disk.
*/
function dinfoBitrate(track) {
  if (track === undefined || !track.cellPs) { return null; }
  return Math.round(1000000000 / track.cellPs);
}

function dinfoDrawDetail() {
  var track = dinfoTracks[dinfoSelected.cylinder * 2 + dinfoSelected.side];

  $('#dinfoTrack').html(dinfoSelected.cylinder);
  $('#dinfoSideSel').html(dinfoSelected.side);

  if (track === undefined) {
    ['#dinfoRpm', '#dinfoBitrate', '#dinfoFormat', '#dinfoLen', '#dinfoSectors',
     '#dinfoSyncs', '#dinfoStrays', '#dinfoCylSeen', '#dinfoSides', '#dinfoInterface']
      .forEach(function (id) { $(id).html("&mdash;"); });
    $('#dinfoTrackViewLabel').html("&mdash;");
    dinfoDrawTrackView(undefined);
    dinfoDrawHistogram(undefined);
    return;
  }

  var us = dinfoTrackUs(track);
  $('#dinfoRpm').html(us ? (Math.round(60000000 / us) + " RPM") : "&mdash;");

  var rate = dinfoBitrate(track);
  $('#dinfoBitrate').html(rate ? (rate + " kbit/s") : "&mdash;");

  // A header that checksummed, not a sync mark. See dinfoTint() for why the two
  // are not the same claim on a surface that has never been written.
  $('#dinfoFormat').html(track.cylSeen >= 0 ? "AMIGA_MFM_ENCODING"
                         : (track.syncs > 0 ? "UNFORMATTED<br/>" + track.syncs + " stray marks"
                                            : "UNFORMATTED"));
  var cells = dinfoTrackCells(track);
  $('#dinfoLen').html((cells == null ? "&mdash;" : (cells + " cells")) +
                      "<br/>" + track.cells + " transitions");
  $('#dinfoSectors').html(track.valid + " of " + track.sectorCount +
                          (track.bad > 0 ? "<br/>" + track.bad + " bad" : ""));
  // Overflow means the table filled and the rest of the track's marks were never
  // looked at, so the counts below it are a floor and not a total.
  $('#dinfoSyncs').html(track.syncs + (track.over > 0 ? " (+" + track.over + " unseen)" : ""));
  $('#dinfoStrays').html(track.strays + " / " + track.dups + " / " + track.trunc);
  $('#dinfoCylSeen').html(track.cylSeen < 0 ? "none" : track.cylSeen);
  $('#dinfoSides').html(dinfoMeta != null && dinfoMeta.side < 0 ? 2 : 1);

  var iface = (track.flags & DINFO_FROMFILE) ? "SCP image" : "Shugart Interface";
  if (!(track.flags & DINFO_ALIGNED)) { iface += " (no index)"; }
  $('#dinfoInterface').html(iface + "<br/>" + ((track.flags & DINFO_HD) ? "HD" : "DD"));

  $('#dinfoTrackViewLabel').html("Cylinder " + track.cylinder + ", side " + track.side);

  dinfoDrawTrackView(track);
  dinfoDrawHistogram(dinfoHists[dinfoSelected.cylinder * 2 + dinfoSelected.side]);
}

// ---------------------------------------------------------------------------
// track view
// ---------------------------------------------------------------------------

/*
   The selected track laid out flat, index to index.

   The disk view answers "which tracks are in trouble"; this answers "what is on
   this one" - where the sectors sit, how big the gaps between them are, and
   whether the surface between them is written or blank.
*/
function dinfoDrawTrackView(track) {
  var canvas = document.getElementById("dinfoTrackCanvas");
  if (canvas == null) { return; }

  /*
     The canvas is sized to the whole zoomed track and the wrapper scrolls it,
     rather than the canvas staying one screen wide and a slider choosing which
     slice to draw. Same picture, but the panning is the browser's: it drags,
     wheels, and takes the keyboard, and it shows how much of the track is off
     screen - which is what the old "x offset (% of the track len)" slider was
     trying to say in words.
  */
  var zoom = parseInt($('#dinfoZoom').val(), 10) || 1;
  var wrap = canvas.parentNode;
  var base = wrap ? wrap.clientWidth : canvas.width;
  if (base < 320) { base = 320; }

  var w = Math.round(base * zoom);
  // Assigning width clears the canvas, so only do it when it actually changed -
  // otherwise every redraw throws away the drawing it is about to repeat.
  if (canvas.width !== w) { canvas.width = w; }
  var h = canvas.height;

  var ctx = canvas.getContext("2d");
  ctx.fillStyle = "#050505";
  ctx.fillRect(0, 0, w, h);

  if (track === undefined) { return; }

  var xOf = function (pos) { return (pos / 4096) * w; };

  // surface density
  var profile = dinfoProfiles[track.cylinder * 2 + track.side];
  if (profile !== undefined) {
    for (var x = 0; x < w; x++) {
      var level = profile[Math.floor((x / w) * DINFO_BUCKETS) % DINFO_BUCKETS];
      var v = Math.round(40 + (level * 12));
      ctx.fillStyle = "rgb(" + Math.round(v * 0.55) + "," + Math.round(v * 0.6) + "," + v + ")";
      ctx.fillRect(x, 20, 1, 42);
    }
  }

  // sectors
  var sectorSpan = track.revBytes > 0 ? (1088 / track.revBytes) * 4096 : 0;
  track.sectors.forEach(function (s) {
    var x0 = xOf(s.pos);
    var x1 = xOf(s.pos + sectorSpan);

    ctx.fillStyle = dinfoVerdictColour(s.verdict, s.sector);
    ctx.fillRect(x0, 66, Math.max(2, x1 - x0), 22);

    ctx.strokeStyle = "#ffffff";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x0, 16);
    ctx.lineTo(x0, 92);
    ctx.stroke();

    if ((x1 - x0) > 18) {
      ctx.fillStyle = "#000000";
      ctx.font = "10px monospace";
      ctx.fillText(s.sector == DINFO_STRAY ? "?" : s.sector, x0 + 4, 81);
    }
  });

  /*
     A tick every 10% of the revolution at 1x, and more of them as it is zoomed
     in: at 16x, ten ticks across sixteen screens is one every screen and a half,
     which is no scale at all. Kept to whole percents so the labels stay short.
  */
  var ticks = 10 * zoom;
  if (ticks > 100) { ticks = 100; }
  ctx.font = "10px monospace";
  for (var i = 0; i <= ticks; i++) {
    var px = (i / ticks) * w;
    ctx.fillStyle = "#1e1e1e";
    ctx.fillRect(px, 16, 1, 76);
    ctx.fillStyle = "#8a8a8a";
    ctx.fillText(Math.round((i / ticks) * 100) + "%", px + 2, 12);
  }

  ctx.fillStyle = "#8a8a8a";
  ctx.fillText("index", 2, h - 2);
  ctx.fillText(track.cells + " transitions", w - 130, h - 2);
}

/*
   The flux interval histogram for the selected track.

   Amiga MFM writes cells 2, 3 and 4us apart, so a healthy track is three clear
   peaks. A smeared or shifted one is what a marginal read, a misaligned head or a
   deliberately odd bitrate looks like before it becomes a checksum failure.
*/
function dinfoDrawHistogram(bins) {
  var canvas = document.getElementById("dinfoHistCanvas");
  if (canvas == null) { return; }
  var ctx = canvas.getContext("2d");
  var w = canvas.width, h = canvas.height;

  ctx.fillStyle = "#050505";
  ctx.fillRect(0, 0, w, h);

  var base = h - 22;

  // gridlines at whole microseconds. A raw bin is one timer tick; the firmware
  // sums four into each of the bins sent, and a tick is 0.04166667us with a
  // 0.25us floor - the same arithmetic printHist() uses on the console.
  ctx.font = "10px monospace";
  for (var us = 2; us <= 8; us++) {
    var tick = (us - 0.25) / 0.04166667;
    var bin = tick / 4;
    var x = (bin / DINFO_HIST_BINS) * w;
    if (x < 0 || x > w) { continue; }
    ctx.fillStyle = "#16324a";
    ctx.fillRect(x, 6, 1, base - 6);
    ctx.fillStyle = "#4caf50";
    ctx.fillText(us + "us", x + 2, h - 8);
  }

  if (bins === undefined) {
    ctx.fillStyle = "#616161";
    ctx.fillText("no histogram for this track", 8, 20);
    return;
  }

  var bw = w / DINFO_HIST_BINS;
  for (var i = 0; i < DINFO_HIST_BINS; i++) {
    var barH = Math.round((bins[i] / 255) * (base - 10));
    if (barH < 1 && bins[i] > 0) { barH = 1; }
    ctx.fillStyle = "#ffb74d";
    ctx.fillRect(i * bw, base - barH, Math.max(1, bw - 1), barH);
  }
}

// ---------------------------------------------------------------------------
// selection
// ---------------------------------------------------------------------------

function dinfoSelect(cylinder, side) {
  if (cylinder < 0) { cylinder = 0; }
  if (cylinder > DINFO_MAX_CYLINDERS - 1) { cylinder = DINFO_MAX_CYLINDERS - 1; }

  dinfoSelected = { cylinder: cylinder, side: side };
  // Not while it is being typed into, the way drivetoolkit.js leaves its cylinder
  // box alone on focus - otherwise a redraw rewrites the number mid keystroke.
  if (!$('#dinfoCylPick').is(':focus')) { $('#dinfoCylPick').val(cylinder); }
  $('#dinfoSideBtn').html("Side: " + side);

  dinfoDrawSide(0);
  dinfoDrawSide(1);
  dinfoDrawDetail();
}

/*
   Step the selected cylinder, and cycle the side.

   Named and shaped like dtNudge() so the two selectors read the same, but local:
   the Drive Toolkit's moves a physical head and goes out over the websocket, this
   one only moves a cursor over a survey already in hand.
*/
function dinfoNudge(delta) {
  dinfoSelect(dinfoSelected.cylinder + delta, dinfoSelected.side);
}

function dinfoCycleSide() {
  dinfoSelect(dinfoSelected.cylinder, dinfoSelected.side === 0 ? 1 : 0);
}

/*
   A checksum pair as a viewer states it: the value that was stored, and whether the
   bytes actually captured still add up to it.
*/
function dinfoCrcText(stored, calc) {
  return "0x" + ("00000000" + (stored >>> 0).toString(16).toUpperCase()).slice(-8) +
         " (" + (stored === calc ? "Ok" : "BAD") + ")";
}

function dinfoPad(value, width) {
  return ("0000000000" + value).slice(-width);
}

/*
   The sector dump: a header saying what the sector claims to be, then its 512
   bytes as hex and ASCII.

   Sixteen bytes to the line rather than the eight a Visual Floppy panel uses -
   there is room for it here, and it halves a 64 line block to 32.
*/
function dinfoDrawDump(rec) {
  var out = document.getElementById("dinfoDumpText");
  if (out == null) { return; }

  if (rec === "pending") { out.textContent = "Decoding\u2026"; return; }
  if (rec == null) { out.textContent = "Hover a sector in the track view."; return; }

  if (!rec.found) {
    out.textContent = "Amiga MFM Sector\n\n" +
      "Sector ID:" + dinfoPad(rec.sector, 3) + "\n\n" +
      "No header in the capture names this sector, so there is nothing to\n" +
      "decode. The mark is on the disk; what follows it did not check out.";
    return;
  }

  var lines = [];
  lines.push("Amiga MFM Sector");
  lines.push("Sector ID:" + dinfoPad(rec.sector, 3));
  // The header carries the LOGICAL track, so the side is its low bit.
  lines.push("Track ID:" + dinfoPad(rec.trackId >> 1, 3) +
             " - Side ID:" + dinfoPad(rec.trackId & 1, 3));
  // Not HxC's "Size:00512 (ID:0x00)". Its ID is a size code; the byte an Amiga
  // header actually carries here is the format, and it is 0xFF. Reusing their
  // wording for a different quantity would read as a decode that disagreed.
  lines.push("Size:00512  Format:0x" + dinfoPad(rec.format.toString(16).toUpperCase(), 2));
  lines.push("Sectors to gap:" + rec.toGap);
  lines.push("Head CRC:" + dinfoCrcText(rec.hdrSum, rec.hdrCalc));
  lines.push("Data CRC:" + dinfoCrcText(rec.dataSum, rec.dataCalc));
  lines.push("Start Sector cell:" + rec.startCell);
  lines.push("Start Sector Data cell:" + rec.dataCell);
  lines.push("End Sector cell:" + rec.endCell);
  lines.push("Number of cells:" + (rec.endCell - rec.startCell));
  lines.push("");

  var perLine = 16;
  for (var off = 0; off < 512; off += perLine) {
    var hex = "", txt = "";
    for (var i = 0; i < perLine; i++) {
      var at = (off + i) * 2;
      var byte = parseInt(rec.hex.substr(at, 2), 16);
      if (isNaN(byte)) { byte = 0; }
      hex += ("0" + byte.toString(16).toUpperCase()).slice(-2) + " ";
      txt += (byte >= 32 && byte < 127) ? String.fromCharCode(byte) : ".";
    }
    lines.push(("000" + off.toString(16).toUpperCase()).slice(-4) + "| " + hex + "| " + txt);
  }

  out.textContent = lines.join("\n");
}

/*
   Which sector of the selected track the pointer is over, or null.

   Same geometry dinfoDrawTrackView() draws with, so what is hit is what is seen.
   Horizontal only: the bands are thin and demanding the pointer be inside the
   22px sector strip makes them fiddly to hit for no gain.
*/
function dinfoTrackHit(canvas, event) {
  var track = dinfoTracks[dinfoSelected.cylinder * 2 + dinfoSelected.side];
  if (track === undefined) { return null; }

  var rect = canvas.getBoundingClientRect();
  if (rect.width === 0) { return null; }

  var x = (event.clientX - rect.left) * (canvas.width / rect.width);
  var w = canvas.width;
  var span = track.revBytes > 0 ? (1088 / track.revBytes) * 4096 : 0;

  for (var i = 0; i < track.sectors.length; i++) {
    var s = track.sectors[i];
    var x0 = (s.pos / 4096) * w;
    var x1 = ((s.pos + span) / 4096) * w;
    if (x >= x0 && x <= Math.max(x0 + 2, x1)) { return s; }
  }
  return null;
}

/*
   Hovering a sector asks the device to decode it.

   Only when the sector under the pointer changes, and only once per sector: on a
   real disk this costs a seek and a revolution, so a pointer crossing a track must
   not turn into a burst of them. dinfoSectorCache holds both the answers and the
   requests in flight.
*/
function dinfoTrackHover(canvas, event) {
  var hit = dinfoTrackHit(canvas, event);
  canvas.style.cursor = hit === null ? "default" : "pointer";

  if (hit === null) { dinfoHoverKey = null; return; }

  var cyl = dinfoSelected.cylinder, side = dinfoSelected.side;
  var label = document.getElementById("dinfoDumpLabel");

  // A stray mark carries no sector number, so there is nothing to ask for.
  if (hit.sector === DINFO_STRAY) {
    dinfoHoverKey = null;
    if (label != null) { label.innerHTML = "Cylinder " + cyl + ", side " + side; }
    dinfoDrawDump({ found: false, sector: 255 });
    return;
  }

  var key = cyl + ":" + side + ":" + hit.sector;
  if (dinfoHoverKey === key) { return; }
  dinfoHoverKey = key;

  if (label != null) {
    label.innerHTML = "Cylinder " + cyl + ", side " + side + ", sector " + hit.sector;
  }

  var cached = dinfoSectorCache[key];
  if (cached === undefined) {
    dinfoSectorCache[key] = "pending";
    wsSend("diskInfoSector," + cyl + "," + side + "," + hit.sector);
    dinfoDrawDump("pending");
  } else {
    dinfoDrawDump(cached);
  }
}

// Which cylinder the pointer is over, or -1 outside the annulus.
function dinfoHitTest(canvas, event) {
  var rect = canvas.getBoundingClientRect();

  // A pane in a tab that is not showing has no layout, so the canvas measures zero
  // wide and the scale below would be Infinity. A pointer cannot reach a hidden
  // canvas, but a NaN hit test would fail silently rather than loudly if one ever
  // did, so it is refused here instead.
  if (rect.width == 0 || rect.height == 0) { return -1; }

  // The canvas is laid out responsively, so a client pixel is not a canvas pixel.
  var x = (event.clientX - rect.left) * (canvas.width / rect.width) - DINFO_CX;
  var y = (event.clientY - rect.top) * (canvas.height / rect.height) - DINFO_CY;

  var r = Math.sqrt(x * x + y * y);
  if (r > DINFO_R_OUTER || r < DINFO_R_HUB) { return -1; }
  return Math.floor((DINFO_R_OUTER - r) / DINFO_RING);
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

function onLoad_DiskInfo() {
  for (var side = 0; side < 2; side++) {
    (function (s) {
      var canvas = document.getElementById("diskCanvas" + s);
      if (canvas == null) { return; }

      canvas.addEventListener("click", function (e) {
        var cyl = dinfoHitTest(canvas, e);
        if (cyl >= 0) { dinfoSelect(cyl, s); }
      });

      canvas.addEventListener("mousemove", function (e) {
        var cyl = dinfoHitTest(canvas, e);
        canvas.style.cursor = cyl >= 0 ? "pointer" : "default";
        if (cyl < 0) { canvas.title = ""; return; }

        var t = dinfoTracks[cyl * 2 + s];
        canvas.title = t === undefined
          ? ("Cylinder " + cyl + ", side " + s + " - not analysed")
          : ("Cylinder " + cyl + ", side " + s + " - " + t.valid + " of " + t.sectorCount +
             " sectors, " + t.syncs + " sync marks");
      });
    })(side);
  }

  (function () {
    var tc = document.getElementById("dinfoTrackCanvas");
    if (tc == null) { return; }
    tc.addEventListener("mousemove", function (e) { dinfoTrackHover(tc, e); });
    tc.addEventListener("mouseleave", function () { dinfoHoverKey = null; });
  })();

  // Enter on the cylinder box jumps straight there, as it does on Head Cal.
  $('#dinfoCylPick').on('keydown', function (e) {
    if (e.key !== 'Enter') { return; }
    e.preventDefault();
    var value = parseInt($('#dinfoCylPick').val(), 10);
    if (!isNaN(value)) { dinfoSelect(value, dinfoSelected.side); }
  });
  $('#dinfoCylPick').on('blur', function () {
    // Put back whatever the selection actually is, so an abandoned edit does not
    // leave a number in the box that nothing on the page agrees with.
    $('#dinfoCylPick').val(dinfoSelected.cylinder);
  });

  $('#dinfoZoom').on('input', function () {
    $('#dinfoZoomValue').html($(this).val() + "x");
    dinfoDrawTrackView(dinfoTracks[dinfoSelected.cylinder * 2 + dinfoSelected.side]);
  });

  // The track canvas is sized from its wrapper, so a window resize changes how
  // much of the track is on screen and it has to be drawn again.
  $(window).on('resize', function () {
    dinfoDrawTrackView(dinfoTracks[dinfoSelected.cylinder * 2 + dinfoSelected.side]);
  });

  /*
     And once more when the tab is first shown. A pane in a tab that is not
     showing has no layout - the same thing dinfoHitTest() guards against - so at
     page load the wrapper measures zero and the canvas falls back to its minimum.
     Everything drawn before the tab is opened would otherwise stay that width
     until something else happened to redraw it.
  */
  $('#diskinfo-tab').on('shown.bs.tab', function () {
    dinfoDrawTrackView(dinfoTracks[dinfoSelected.cylinder * 2 + dinfoSelected.side]);
  });

  dinfoDrawAll();
}
