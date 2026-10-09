// Copyright 2026 Andreas Jansson.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// plaits-analyze: measurements on the stereo WAV files written by plaits-sim
// (MAIN = left channel, AUX = right channel).
//
// Usage:
//   plaits-analyze levels FILE [FROM TO]
//       RMS and peak level of each channel, and their correlation.
//   plaits-analyze pitch FILE CHANNEL [FROM TO]
//       Fundamental frequency of main or aux (or mid, their sum), and the
//       corresponding MIDI note.
//   plaits-analyze balance FILE [FROM TO]
//       Stereo image in octave bands, to check that a stereo signal is
//       centered at all frequencies: level of aux relative to main (in dB),
//       lean (the phase difference between the two channels pulls the image
//       towards the channel which leads: +1 when main leads by 90 degrees,
//       -1 when aux leads) and correlation (1 for a mono signal, 0 for
//       unrelated channels).
//   plaits-analyze image FILE [FROM TO]
//       Summary of the stereo image: the largest level difference between
//       the two channels in an octave band (in dB, in the bands at most 30dB
//       below the loudest one), the lean and the correlation (as above), and
//       the correlation of the bands below 200Hz (the bass, if there is
//       any: none otherwise).
//   plaits-analyze diff A B [FROM TO]
//       Largest difference between two recordings, in units of the least
//       significant bit (1/32768). A and B are file names, optionally
//       followed by :main or :aux to compare a single channel (for example
//       duo.wav:aux duo.wav:main). Without a channel, both channels of the
//       two files are compared.
//
// FROM and TO are times in seconds (default: the whole file).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

struct Audio {
  int sample_rate;
  vector<double> channel[2];
};

bool ReadWav(const char* path, Audio* audio) {
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    perror(path);
    return false;
  }
  char riff[12];
  if (fread(riff, 1, 12, fp) != 12 || memcmp(riff, "RIFF", 4) ||
      memcmp(riff + 8, "WAVE", 4)) {
    fprintf(stderr, "%s: not a WAV file\n", path);
    fclose(fp);
    return false;
  }
  int channels = 0;
  int bits = 0;
  audio->sample_rate = 0;
  char id[4];
  unsigned int size;
  while (fread(id, 1, 4, fp) == 4 && fread(&size, 4, 1, fp) == 1) {
    if (!memcmp(id, "fmt ", 4)) {
      unsigned char fmt[16];
      if (size < 16 || fread(fmt, 1, 16, fp) != 16) {
        break;
      }
      channels = fmt[2] | (fmt[3] << 8);
      audio->sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | \
          (fmt[7] << 24);
      bits = fmt[14] | (fmt[15] << 8);
      fseek(fp, size - 16, SEEK_CUR);
    } else if (!memcmp(id, "data", 4)) {
      if (channels != 2 || bits != 16) {
        fprintf(stderr, "%s: expected 16-bit stereo\n", path);
        fclose(fp);
        return false;
      }
      vector<short> data(size / 2);
      size_t n = fread(&data[0], 2, data.size(), fp);
      for (size_t i = 0; i + 1 < n; i += 2) {
        audio->channel[0].push_back(data[i] / 32768.0);
        audio->channel[1].push_back(data[i + 1] / 32768.0);
      }
      fclose(fp);
      return true;
    } else {
      fseek(fp, size + (size & 1), SEEK_CUR);
    }
  }
  fprintf(stderr, "%s: no audio data\n", path);
  fclose(fp);
  return false;
}

void Range(const Audio& audio, int argc, char** argv, int first,
           size_t* start, size_t* end) {
  size_t n = audio.channel[0].size();
  *start = 0;
  *end = n;
  if (argc > first + 1) {
    *start = static_cast<size_t>(atof(argv[first]) * audio.sample_rate);
    *end = static_cast<size_t>(atof(argv[first + 1]) * audio.sample_rate);
  }
  if (*end > n) *end = n;
  if (*start > *end) *start = *end;
}

int Levels(const Audio& audio, size_t start, size_t end) {
  const char* names[2] = { "main", "aux" };
  double sum[2] = { 0, 0 };
  double sum_sq[2] = { 0, 0 };
  double peak[2] = { 0, 0 };
  double cross = 0.0;
  size_t n = end - start;
  for (size_t i = start; i < end; ++i) {
    for (int c = 0; c < 2; ++c) {
      double x = audio.channel[c][i];
      sum[c] += x;
      sum_sq[c] += x * x;
      peak[c] = max(peak[c], fabs(x));
    }
    cross += audio.channel[0][i] * audio.channel[1][i];
  }
  double rms[2];
  for (int c = 0; c < 2; ++c) {
    rms[c] = n ? sqrt(sum_sq[c] / n) : 0.0;
    printf("%s: rms=%.4f peak=%.4f\n", names[c], rms[c], peak[c]);
  }
  double mean[2] = { n ? sum[0] / n : 0, n ? sum[1] / n : 0 };
  double var_0 = sum_sq[0] / n - mean[0] * mean[0];
  double var_1 = sum_sq[1] / n - mean[1] * mean[1];
  double covariance = cross / n - mean[0] * mean[1];
  double correlation = (var_0 > 0 && var_1 > 0)
      ? covariance / sqrt(var_0 * var_1)
      : 1.0;
  printf("correlation: %.3f\n", correlation);
  return 0;
}

// Autocorrelation, with parabolic interpolation of the first peak which is
// at least 80% as high as the highest peak (to avoid octave errors).
int Pitch(const Audio& audio, const string& channel, size_t start,
          size_t end) {
  vector<double> x;
  for (size_t i = start; i < end; ++i) {
    double l = audio.channel[0][i];
    double r = audio.channel[1][i];
    x.push_back(channel == "main" ? l : (channel == "aux" ? r : l + r));
  }
  double mean = 0.0;
  for (size_t i = 0; i < x.size(); ++i) mean += x[i];
  mean /= x.size() ? x.size() : 1;
  for (size_t i = 0; i < x.size(); ++i) x[i] -= mean;

  const double sr = audio.sample_rate;
  const size_t min_lag = static_cast<size_t>(sr / 4000.0);
  const size_t max_lag = static_cast<size_t>(sr / 20.0);
  if (x.size() < 2 * max_lag) {
    fprintf(stderr, "segment too short\n");
    return 1;
  }
  const size_t n = x.size() - max_lag;
  vector<double> ac(max_lag + 2, 0.0);
  for (size_t lag = 0; lag <= max_lag + 1 && lag < x.size(); ++lag) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) {
      s += x[i] * x[i + lag];
    }
    ac[lag] = s;
  }
  if (ac[0] <= 0.0) {
    printf("pitch: none\n");
    return 0;
  }
  double highest = 0.0;
  for (size_t lag = min_lag; lag <= max_lag; ++lag) {
    highest = max(highest, ac[lag]);
  }
  size_t best = 0;
  for (size_t lag = min_lag; lag <= max_lag; ++lag) {
    if (ac[lag] >= 0.8 * highest && ac[lag] >= ac[lag - 1] &&
        ac[lag] >= ac[lag + 1]) {
      best = lag;
      break;
    }
  }
  if (!best) {
    printf("pitch: none\n");
    return 0;
  }
  double a = ac[best - 1], b = ac[best], c = ac[best + 1];
  double denominator = a - 2.0 * b + c;
  double lag = best + (denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0);
  double frequency = sr / lag;
  // The firmware runs at 47872.34Hz, the WAV header says 47872Hz.
  frequency *= 47872.34 / 47872.0;
  double note = 69.0 + 12.0 * log2(frequency / 440.0);
  printf("pitch: %.2f Hz\n", frequency);
  printf("note: %.2f\n", note);
  return 0;
}

// Band-pass biquad (RBJ cookbook, 0dB peak gain). Four of them in series make
// an octave band filter.
struct Biquad {
  double b0, b1, b2, a1, a2;
  double x1, x2, y1, y2;

  void Init(double frequency, double q, double sample_rate) {
    double w = 2.0 * M_PI * frequency / sample_rate;
    double alpha = sin(w) / (2.0 * q);
    double a0 = 1.0 + alpha;
    b0 = alpha / a0;
    b1 = 0.0;
    b2 = -alpha / a0;
    a1 = -2.0 * cos(w) / a0;
    a2 = (1.0 - alpha) / a0;
    x1 = x2 = y1 = y2 = 0.0;
  }

  double Process(double x) {
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

// Hilbert transformer (Olli Niemitalo's pair of allpass chains, whose phase
// responses are 90 degrees apart from 20Hz to 22kHz): the outputs of
// Process are the real and imaginary parts of the analytic signal.
class Analytic {
 public:
  Analytic() {
    static const double a[2][4] = {
      { 0.479400866, 0.876218494, 0.976597590, 0.997499256 },
      { 0.161758498, 0.733028932, 0.945349700, 0.990599157 }
    };
    for (int c = 0; c < 2; ++c) {
      for (int i = 0; i < 4; ++i) {
        a_[c][i] = a[c][i];
      }
      for (int i = 0; i < 5; ++i) {
        x1_[c][i] = x2_[c][i] = 0.0;
      }
    }
    delay_ = 0.0;
  }

  void Process(double x, double* re, double* im) {
    double y[2];
    for (int c = 0; c < 2; ++c) {
      double v = x;
      for (int i = 0; i < 4; ++i) {
        double out = a_[c][i] * (v + x2_[c][i + 1]) - x2_[c][i];
        x2_[c][i] = x1_[c][i];
        x1_[c][i] = v;
        v = out;
      }
      x2_[c][4] = x1_[c][4];
      x1_[c][4] = v;
      y[c] = v;
    }
    *re = delay_;
    delay_ = y[0];
    *im = y[1];
  }

 private:
  double a_[2][4];
  double x1_[2][5];
  double x2_[2][5];
  double delay_;
};

struct BandStereo {
  double rms[2];
  double correlation;  // 1: same signal, 0: unrelated (or 90 degrees apart).
  double lean;  // Positive when main leads, negative when aux leads.
};

BandStereo AnalyzeBand(const Audio& audio, size_t start, size_t end,
                       double frequency) {
  const int num_stages = 4;
  Biquad f[2][num_stages];
  Analytic h[2];
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < num_stages; ++i) {
      f[c][i].Init(frequency, 2.0, audio.sample_rate);
    }
  }
  double power[2] = { 0.0, 0.0 };
  double cross_re = 0.0;
  double cross_im = 0.0;
  // Starts a little early, to let the filters settle.
  size_t settle = static_cast<size_t>(audio.sample_rate * 0.05);
  size_t first = start > settle ? start - settle : 0;
  for (size_t i = first; i < end; ++i) {
    double re[2], im[2], y[2];
    for (int c = 0; c < 2; ++c) {
      y[c] = audio.channel[c][i];
      for (int s = 0; s < num_stages; ++s) {
        y[c] = f[c][s].Process(y[c]);
      }
      h[c].Process(y[c], &re[c], &im[c]);
    }
    if (i < start) {
      continue;
    }
    for (int c = 0; c < 2; ++c) {
      power[c] += re[c] * re[c] + im[c] * im[c];
    }
    // conj(main) * aux (the imaginary output of the Hilbert transformer
    // lags behind the real one).
    cross_re += re[0] * re[1] + im[0] * im[1];
    cross_im += re[0] * im[1] - im[0] * re[1];
  }
  BandStereo result;
  size_t n = end > start ? end - start : 1;
  double norm = sqrt(power[0] * power[1]);
  for (int c = 0; c < 2; ++c) {
    // The analytic signal has twice the power of the real signal.
    result.rms[c] = sqrt(power[c] / (2.0 * n));
  }
  result.correlation = norm > 0.0 ? cross_re / norm : 1.0;
  result.lean = norm > 0.0 ? cross_im / norm : 0.0;
  return result;
}

double Decibels(double x) {
  return 20.0 * log10(max(x, 1e-9));
}

// Bands whose level is more than 30dB below the loudest one are not shown.
int Balance(const Audio& audio, size_t start, size_t end) {
  const double bands[] = {
    62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0
  };
  const int num_bands = sizeof(bands) / sizeof(bands[0]);
  BandStereo band[num_bands];
  double loudest = 0.0;
  for (int b = 0; b < num_bands; ++b) {
    band[b] = AnalyzeBand(audio, start, end, bands[b]);
    loudest = max(loudest, max(band[b].rms[0], band[b].rms[1]));
  }
  // Overall lean and correlation: averages weighted by the energy of the
  // bands. The lean is averaged over the bands in which the phase difference
  // between the ears matters most for localization (250Hz to 2kHz).
  double lean = 0.0, lean_weight = 0.0;
  double correlation = 0.0, correlation_weight = 0.0;
  for (int b = 0; b < num_bands; ++b) {
    double level = max(band[b].rms[0], band[b].rms[1]);
    double energy = band[b].rms[0] * band[b].rms[0] + \
        band[b].rms[1] * band[b].rms[1];
    correlation += energy * band[b].correlation;
    correlation_weight += energy;
    if (bands[b] >= 250.0 && bands[b] <= 2000.0) {
      lean += energy * band[b].lean;
      lean_weight += energy;
    }
    if (level == 0.0 || Decibels(level) < Decibels(loudest) - 30.0) {
      continue;
    }
    printf("%5.0fHz: aux %+5.1f dB, lean %+.2f, correlation %+.2f\n",
           bands[b],
           Decibels(band[b].rms[1]) - Decibels(band[b].rms[0]),
           band[b].lean,
           band[b].correlation);
  }
  printf("overall: lean %+.2f, correlation %+.2f\n",
         lean_weight > 0.0 ? lean / lean_weight : 0.0,
         correlation_weight > 0.0 ? correlation / correlation_weight : 1.0);
  return 0;
}

int Image(const Audio& audio, size_t start, size_t end) {
  const double bands[] = {
    62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0
  };
  const int num_bands = sizeof(bands) / sizeof(bands[0]);
  BandStereo band[num_bands];
  double loudest = 0.0;
  for (int b = 0; b < num_bands; ++b) {
    band[b] = AnalyzeBand(audio, start, end, bands[b]);
    loudest = max(loudest, max(band[b].rms[0], band[b].rms[1]));
  }
  double level_difference = 0.0;
  double lean = 0.0, lean_weight = 0.0;
  double correlation = 0.0, correlation_weight = 0.0;
  double bass = 0.0, bass_weight = 0.0;
  for (int b = 0; b < num_bands; ++b) {
    double level = max(band[b].rms[0], band[b].rms[1]);
    double energy = band[b].rms[0] * band[b].rms[0] + \
        band[b].rms[1] * band[b].rms[1];
    correlation += energy * band[b].correlation;
    correlation_weight += energy;
    if (bands[b] >= 250.0 && bands[b] <= 2000.0) {
      lean += energy * band[b].lean;
      lean_weight += energy;
    }
    if (level == 0.0 || Decibels(level) < Decibels(loudest) - 30.0) {
      continue;
    }
    level_difference = max(level_difference, fabs(
        Decibels(band[b].rms[1]) - Decibels(band[b].rms[0])));
    if (bands[b] < 200.0) {
      bass += energy * band[b].correlation;
      bass_weight += energy;
    }
  }
  printf("level difference: %.1f dB\n", level_difference);
  printf("lean: %+.2f\n", lean_weight > 0.0 ? lean / lean_weight : 0.0);
  printf("correlation: %+.2f\n",
         correlation_weight > 0.0 ? correlation / correlation_weight : 1.0);
  if (bass_weight > 0.0) {
    printf("bass correlation: %+.2f\n", bass / bass_weight);
  } else {
    printf("bass correlation: none\n");
  }
  return 0;
}

double MaxDiff(const vector<double>& a, const vector<double>& b,
               size_t start, size_t end) {
  double largest = 0.0;
  for (size_t i = start; i < end && i < a.size() && i < b.size(); ++i) {
    largest = max(largest, fabs(a[i] - b[i]));
  }
  return largest * 32768.0;
}

// Splits "file.wav:main" into the file name and the channel (-1 if none).
string ParseSource(const string& source, int* channel) {
  *channel = -1;
  size_t colon = source.rfind(':');
  if (colon != string::npos) {
    string suffix = source.substr(colon + 1);
    if (suffix == "main" || suffix == "aux") {
      *channel = suffix == "main" ? 0 : 1;
      return source.substr(0, colon);
    }
  }
  return source;
}

int Diff(int argc, char** argv) {
  int channel_a, channel_b;
  string path_a = ParseSource(argv[2], &channel_a);
  string path_b = ParseSource(argv[3], &channel_b);
  if ((channel_a == -1) != (channel_b == -1)) {
    fprintf(stderr, "specify a channel for both recordings, or for none\n");
    return 1;
  }
  Audio a, b;
  if (!ReadWav(path_a.c_str(), &a) || !ReadWav(path_b.c_str(), &b)) {
    return 1;
  }
  size_t start, end;
  Range(a, argc, argv, 4, &start, &end);
  if (a.channel[0].size() != b.channel[0].size()) {
    printf("lengths differ: %d and %d samples\n",
           int(a.channel[0].size()), int(b.channel[0].size()));
  }
  if (channel_a == -1) {
    printf("main: max_diff=%.0f\n",
           MaxDiff(a.channel[0], b.channel[0], start, end));
    printf("aux: max_diff=%.0f\n",
           MaxDiff(a.channel[1], b.channel[1], start, end));
  } else {
    printf("max_diff=%.0f\n",
           MaxDiff(a.channel[channel_a], b.channel[channel_b], start, end));
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
        "Usage:\n"
        "  %s levels FILE [FROM TO]\n"
        "  %s pitch FILE main|aux|mid [FROM TO]\n"
        "  %s balance FILE [FROM TO]\n"
        "  %s image FILE [FROM TO]\n"
        "  %s diff A[:main|:aux] B[:main|:aux] [FROM TO]\n",
        argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }
  string command(argv[1]);
  if (command == "diff" && argc >= 4) {
    return Diff(argc, argv);
  }
  Audio audio;
  if (!ReadWav(argv[2], &audio)) {
    return 1;
  }
  size_t start, end;
  if (command == "levels") {
    Range(audio, argc, argv, 3, &start, &end);
    return Levels(audio, start, end);
  } else if (command == "balance") {
    Range(audio, argc, argv, 3, &start, &end);
    return Balance(audio, start, end);
  } else if (command == "image") {
    Range(audio, argc, argv, 3, &start, &end);
    return Image(audio, start, end);
  } else if (command == "pitch" && argc >= 4) {
    Range(audio, argc, argv, 4, &start, &end);
    return Pitch(audio, argv[3], start, end);
  }
  fprintf(stderr, "invalid command\n");
  return 1;
}
