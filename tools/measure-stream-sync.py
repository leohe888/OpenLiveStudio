"""Measure the synthetic flash/beep signal emitted by tst_streaming::srsEndurance.

Usage: python tools/measure-stream-sync.py media.mp4 --ffmpeg path/to/ffmpeg.exe
Works with a locally recorded MP4 or an HTTP-FLV sample captured without re-encoding.
"""
import argparse
import array
import bisect
import json
import math
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("media", type=pathlib.Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()

    def command(binary, options):
        return subprocess.run([binary, *options], check=True, capture_output=True).stdout

    info = json.loads(command(args.ffprobe, ["-v", "error", "-show_streams", "-show_format", "-of", "json", str(args.media)]))
    video = next(s for s in info["streams"] if s["codec_type"] == "video")
    audio = next(s for s in info["streams"] if s["codec_type"] == "audio")
    numerator, denominator = map(int, video["r_frame_rate"].split("/"))
    fps = numerator / denominator
    origin = float(info.get("format", {}).get("start_time", 0))
    packet_info = json.loads(command(args.ffprobe, ["-v", "error", "-select_streams", "v:0", "-show_packets",
                                                     "-show_entries", "packet=pts_time", "-of", "json", str(args.media)]))
    timestamps = [float(p["pts_time"])-origin for p in packet_info["packets"]]
    base = ["-v", "error", "-threads", "1", "-filter_threads", "1", "-copyts", "-start_at_zero", "-i", str(args.media)]
    pixels = command(args.ffmpeg, [*base, "-map", "0:v:0", "-vf", "crop=8:8:60:60,scale=1:1,format=gray",
                                   "-fps_mode", "passthrough", "-f", "rawvideo", "pipe:1"])
    pcm = command(args.ffmpeg, [*base, "-map", "0:a:0", "-af", "aresample=async=1:first_pts=0",
                                "-ac", "1", "-ar", "8000", "-f", "f32le", "pipe:1"])
    samples = array.array("f")
    samples.frombytes(pcm)
    del pcm

    def edges(active, step, start, times=None):
        result, previous = [], False
        for index, value in enumerate(active):
            if value and not previous:
                time = times[index] if times is not None else start + index * step
                if not result or time - result[-1] > 0.5:
                    result.append(time)
            previous = value
        return result

    if len(timestamps) != len(pixels):
        raise SystemExit("Decoded frame count differs from packet count; this check requires the no-B-frame test encoder.")
    visual = edges((p > 150 for p in pixels), 1 / fps, 0, timestamps)
    auditory = edges((math.sqrt(sum(x*x for x in samples[i:i+80]) / 80) > 0.1
                      for i in range(0, len(samples)-79, 80)), 0.01, 0)
    offsets = []
    duration = timestamps[-1] + 1/fps
    for flash in visual:
        if flash < 1 or flash > duration - 1:
            continue
        index = bisect.bisect_left(auditory, flash)
        candidates = auditory[max(0, index-1):index+1]
        if candidates:
            offsets.append(min(candidates, key=lambda beep: abs(beep-flash)) - flash)
    if len(offsets) < max(3, int(duration)-5):
        raise SystemExit("Too few matching flash/beep events; this file may not contain the synthetic signal.")
    count = min(30, len(offsets))
    first, last = sum(offsets[:count])/count, sum(offsets[-count:])/count
    report = {"file": str(args.media.resolve()), "videoFrames": len(pixels), "durationSeconds": duration,
              "pairedEvents": len(offsets), "maximumSyncErrorMs": max(map(abs, offsets))*1000,
              "firstMeanOffsetMs": first*1000, "lastMeanOffsetMs": last*1000,
              "driftMs": abs(last-first)*1000}
    print(json.dumps(report, indent=2))
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    if report["maximumSyncErrorMs"] > 150:
        raise SystemExit("Audio/video offset exceeded 150 ms.")


if __name__ == "__main__":
    main()
