# Writes example sounds for the engine (mono 16-bit PCM WAV). Usage: perl makewav.pl <assets/sounds dir>
use strict; use warnings;
my $dir = shift or die "dir";
my $pi = 3.14159265358979;
sub write_wav {
    my ($path, $rate, $samples) = @_;
    my $data = pack('s<*', map { my $v = int($_ * 32767); $v > 32767 ? 32767 : ($v < -32768 ? -32768 : $v) } @$samples);
    my $fmt = pack('v v V V v v', 1, 1, $rate, $rate * 2, 2, 16);
    my $body = 'WAVE' . 'fmt ' . pack('V', length $fmt) . $fmt . 'data' . pack('V', length $data) . $data;
    open my $f, '>:raw', $path or die $path; print $f 'RIFF' . pack('V', length $body) . $body; close $f;
}
# hit: a short "pip" sliding down from 1200 Hz to 500 Hz, 0.15 s, fading out.
{
    my $rate = 44100; my $n = int(0.15 * $rate); my @s; my $phase = 0;
    for my $i (0 .. $n - 1) {
        my $t = $i / $n;
        my $freq = 1200 - 700 * $t;
        $phase += 2 * $pi * $freq / $rate;
        my $env = (1 - $t) ** 2;
        $env *= $i / 220 if $i < 220; # 5 ms fade in
        push @s, 0.5 * $env * sin($phase);
    }
    write_wav("$dir/hit.wav", $rate, \@s);
}
# bgm: a calm 4-second loop (C - Am - F - G, one chord per second, soft arpeggio).
{
    my $rate = 22050; my @s;
    my @chords = ([261.63, 329.63, 392.00], [220.00, 261.63, 329.63], [174.61, 220.00, 261.63], [196.00, 246.94, 293.66]);
    my $noteLen = int($rate / 4); # 4 notes per chord
    for my $chord (@chords) {
        for my $k (0 .. 3) {
            my $freq = $chord->[$k % 3] * ($k == 3 ? 2 : 1);
            my $bass = $chord->[0] / 2;
            for my $i (0 .. $noteLen - 1) {
                my $t = $i / $rate;
                my $env = exp(-4 * $i / $noteLen) * ($i < 100 ? $i / 100 : 1) * ($i > $noteLen - 100 ? ($noteLen - $i) / 100 : 1);
                my $benv = ($i < 100 && $k == 0) ? $i / 100 : (($i > $noteLen - 100 && $k == 3) ? ($noteLen - $i) / 100 : 1);
                push @s, 0.25 * $env * sin(2 * $pi * $freq * $t) + 0.12 * $benv * sin(2 * $pi * $bass * (($k * $noteLen + $i) / $rate));
            }
        }
    }
    write_wav("$dir/bgm.wav", $rate, \@s);
}
