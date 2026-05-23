# Amiga MOD 格式说明文档

## 文章信息

* **文章编号**：1391 of comp.sys.ibm.pc.soundcard
* **转发版本**：VMS News - V6.0 13/10/90 VAX/VMS V5.5; site darwin.ntu.edu.au
* **路径**：
  `darwin.ntu.edu.au!munnari.oz.au!spool.mu.edu!uunet!mcsun!news.funet.fi!network.jyu.fi!ap`
* **新闻组**：`comp.sys.ibm.pc.soundcard`
* **主题**：Amiga MOD format, here it is.
* **Message-ID**：`<1992Sep4.052947.29727@jyu.fi>`
* **作者**：Patric Aalto (`ap@jyu.fi`)
* **日期**：Fri, 4 Sep 1992 05:29:47 GMT
* **机构**：University of Jyvaskyla, Finland
* **行数**：530

---

你好！

这里是 Amiga（ProTracker）模块格式文档。第一部分是我从某个 BBS 获取的，第二部分来自 Mark Cox（ModPlay 等程序的作者）。

Patrick Aalto
[ap@tukki.jyu.fi](mailto:ap@tukki.jyu.fi)

---

## Protracker 1.1B Song/Module 格式

| 偏移 | 字节数 | 描述                           |
| -- | --: | ---------------------------- |
| 0  |  20 | 歌曲名称。记得在末尾填充空字节（null bytes）。 |

---

## Sample 1-31 信息

### Sample 1

| 偏移 | 字节数 | 描述                                      |
| -- | --: | --------------------------------------- |
| 20 |  22 | Sample 1 名称。使用空字节填充。                    |
| 42 |   2 | Sample 1 长度。以 word 数量存储。乘以 2 得到真实字节长度。  |
| 44 |   1 | 低四位为 finetune 值，以有符号四位数存储。高四位未使用，应设为 0。 |

### Finetune 对应表

| 值 | Finetune |
| - | -------- |
| 0 | 0        |
| 1 | +1       |
| 2 | +2       |
| 3 | +3       |
| 4 | +4       |
| 5 | +5       |
| 6 | +6       |
| 7 | +7       |
| 8 | -8       |
| 9 | -7       |
| A | -6       |
| B | -5       |
| C | -4       |
| D | -3       |
| E | -2       |
| F | -1       |

| 偏移 | 字节数 | 描述                                                     |
| -- | --: | ------------------------------------------------------ |
| 45 |   1 | Sample 1 音量。范围 `$00-$40`，即十进制 `0-64`。                  |
| 46 |   2 | Sample 1 循环起点。以相对于 sample 起始位置的 word 偏移存储。乘以 2 得到字节偏移。 |
| 48 |   2 | Sample 1 循环长度。以 loop 中 word 数量存储。乘以 2 得到字节长度。          |

---

后续 30 个 sample 的信息结构与 Sample 1 相同。

| 偏移  | 字节数 | 描述        |
| --- | --: | --------- |
| 50  |  30 | Sample 2  |
| 80  |  30 | Sample 3  |
| ... | ... | ...       |
| 890 |  30 | Sample 30 |
| 920 |  30 | Sample 31 |

---

| 偏移   | 字节数 | 描述                                                                                                                                           |
| ---- | --: | -------------------------------------------------------------------------------------------------------------------------------------------- |
| 950  |   1 | Song length。范围 `1-128`。                                                                                                                      |
| 951  |   1 | 此字节通常设为 `127`，用于让旧 tracker 在加载时扫描所有 pattern。Noisetracker 将其用于 restart，但 Protracker 不使用。                                                      |
| 952  | 128 | Song positions 0-127。每个值范围 `0-63`，表示该位置播放哪个 pattern。                                                                                         |
| 1080 |   4 | 四个字符 `"M.K."`。这是 Mahoney & Kaktus 在 sample 数量从 15 增加到 31 时加入的标记。如果不存在，则模块使用 15 个 sample，或文本已被移除以防止 rip。Startrekker 则会写入 `"FLT4"` 或 `"FLT8"`。 |

---

| 偏移   |  字节数 | 描述            |
| ---- | ---: | ------------- |
| 1084 | 1024 | Pattern 00 数据 |
| ...  |  ... | ...           |

存储的 pattern 数量等于 song position 表（偏移 `952-1079`）中的最大 pattern 编号。

---

## Pattern 数据结构

每个 note 使用 4 字节存储，并且 pattern 中每个位置的四个 channel 数据按顺序排列：

```text
00 -  chan1  chan2  chan3  chan4
01 -  chan1  chan2  chan3  chan4
02 -  chan1  chan2  chan3  chan4
etc.
```

---

## 每个 Note 的结构

```text
 _____byte 1_____   byte2_    _____byte 3_____   byte4_
/                / /      /  /                / /      /
0000          0000-00000000  0000          0000-00000000
```

* Sample 编号高四位
* 12 位音符 period
* Sample 编号低四位
* Effect command

---

## Tuning 0（Normal）Period 表

| 音阶        | Period                                          |
| --------- | ----------------------------------------------- |
| C-1 到 B-1 | 856,808,762,720,678,640,604,570,538,508,480,453 |
| C-2 到 B-2 | 428,404,381,360,339,320,302,285,269,254,240,226 |
| C-3 到 B-3 | 214,202,190,180,170,160,151,143,135,127,120,113 |

要确定显示的音符，应扫描该表，找到与 byte 1-2 中 period 相同的值，再通过索引查找音符名称表。

---

普通歌曲的数据格式如上。压缩歌曲以 `"PACK"` 四字符开头，但作者并不知道压缩方式。如果需要，可以提供 cruncher/decruncher 的源码，但作者本人并不理解其工作方式；只是从另一个 tracker 中提取而来。

在 module 中，所有 sample 数据都紧跟在 pattern 数据之后。
要确定 sample 的起止位置，需要使用文件开头（偏移 20）中的 sample 信息结构。查看播放程序中的 `mt_init` 例程即可了解具体实现。

Lars "ZAP" Hamre / Amiga Freelancers

---

# EFFECT COMMANDS（效果命令）

```text
0 - None/Arpeggio     8 - * NOT USED *
1 - Portamento Up     9 - SampleOffset
2 - Portamento Down   A - VolumeSlide
3 - TonePortamento    B - PositionJump
4 - Vibrato           C - Set Volume
5 - ToneP + VolSlide  D - PatternBreak
6 - Vibra + VolSlide  E - Misc. Cmds
7 - Tremolo           F - Set Speed
```

---

# E 类命令

```text
E0- Filter On/Off     E8- * NOT USED *
E1- Fineslide Up      E9- Retrig Note
E2- Fineslide Down    EA- FineVol Up
E3- Glissando Control EB- FineVol Down
E4- Vibrato Control   EC- NoteCut
E5- Set Finetune      ED- NoteDelay
E6- Patternloop       EE- PatternDelay
E7- Tremolo Control   EF- Invert Loop
```

---

# Cmd 0. Arpeggio

**范围：** `$0-$F / $0-$F`

```text
Usage: $0 + 第一个半音偏移
           + 第二个半音偏移
```

Arpeggio 用于模拟和弦。
方法是快速在 3（或 2）个不同音符之间切换音高。

在多数 sample 上听起来会非常嘈杂、颗粒感很强，但在单音 sample 上效果不错。

示例：

```text
C-300047  C 大和弦
(C+E+G 或 C+4+7 半音)

C-300037  C 小和弦
(C+D#+G 或 C+3+7 半音)
```

---

# Cmd 1. Portamento Up

**范围：** `$00-$FF`

```text
Usage: $1 + 滑音速度
```

向上滑音会持续提高 sample 的音高。

不能高于 `B-3`（Period 113）。

示例：

```text
C-300103
```

* `1` 为命令
* `3` 为滑音速度

注意：
滑音会按歌曲速度（speed）重复调用。
因此有时很难精确滑音。
如果改变 speed 而不改变滑音速率，声音会变差。

---

# Cmd 2. Portamento Down

**范围：** `$00-$FF`

```text
Usage: $2 + 滑音速度
```

与命令 1 类似，但会向下滑音（增加 period）。

不能低于 `C-1`（Period 856）。

示例：

```text
C-300203
```

---

# Cmd 3. Tone Portamento

**范围：** `$00-$FF`

```text
Usage: 目标音符 + $3 + 滑音速度
```

自动从旧音符滑向新音符。

无需关心方向，只需设置滑音速度。
若要继续滑动，使用：

```text
$3 + 00
```

示例：

```text
A-200000
```

先播放一个音。

```text
C-300305
```

* `C-3` 为目标音
* `3` 为命令
* `5` 为速度

---

# Cmd 4. Vibrato

**范围：** `Rate:$0-$F, Depth:$0-$F`

```text
Usage: $4 + 振动速度 + 振动深度
```

示例：

```text
C-300481
```

* `4` 为命令
* `8` 为 vibrato 速度
* `1` 为 vibrato 深度

继续 vibrato：

```text
$4 + 00
```

可通过 E4 修改 vibrato 波形。

---

# Cmd 5. TonePortamento + VolumeSlide

```text
Usage: $5 + 音量上滑速度 + 音量下滑速度
```

继续当前 tone portamento，并同时滑动音量。

---

# Cmd 6. Vibrato + VolumeSlide

```text
Usage: $6 + 音量上滑速度 + 音量下滑速度
```

继续当前 vibrato，并同时滑动音量。

---

# Cmd 7. Tremolo

```text
Usage: $7 + tremolo 速度 + tremolo 深度
```

Tremolo 会振荡音量。

---

# Cmd 9. Set SampleOffset

```text
Usage: $9 + SampleOffset
```

从 sample 指定偏移处开始播放，而不是从开头。

适合语音 sample。

示例：

```text
C-300923
```

从 `$2300` 偏移播放。

---

# Cmd A. VolumeSlide

```text
Usage: $A + 上滑速度 + 下滑速度
```

注意：
音量滑动会按歌曲 speed 重复调用。
歌曲越慢，每次 note 的音量变化越大。

---

# Cmd B. Position Jump

```text
Usage: $B + 跳转位置
```

跳转到指定 song position。

同时会执行 Pattern Break。

---

# Cmd C. Set Volume

```text
Usage: $C + 新音量
```

设置当前音量。最大值为 `$40`。

所有音量均使用十六进制表示。

---

# Cmd D. Pattern Break

```text
Usage: $D + pattern position
```

跳转到下一个 song position，并从指定 pattern 行继续播放。

---

# Cmd E0. Set Filter

```text
Usage: $E0 + filter status
```

控制部分 A500 / A2000 的声音滤波器。

---

# Cmd E1. Fine Slide Up

仅在 note 开始时向上滑动一次。

---

# Cmd E2. Fine Slide Down

仅在 note 开始时向下滑动一次。

---

# Cmd E3. Glissando Control

与 Tone Portamento 配合使用。

开启后，tone portamento 会按半音阶滑动，而不是连续滑动。

---

# Cmd E4. Set Vibrato Waveform

可设置：

* Sine
* Ramp Down
* Square Wave
* Random

---

# Cmd E5. Set Finetune

```text
Usage: $E5 + finetune value
```

Finetune 对照：

| Finetune | Value |
| -------- | ----- |
| +7 到 0   | 7 到 0 |
| -1 到 -8  | F 到 8 |

---

# Cmd E6. Pattern Loop

```text
Usage: $E6 + loop count
```

循环 pattern 的某一部分。

---

# Cmd E7. Set Tremolo Waveform

与 E4 类似，但用于 tremolo。

---

# Cmd E9. Retrig Note

```text
Usage: $E9 + retrig tick
```

在下一 note 前重新触发当前 note。

常用于 hi-hat 效果。

---

# Cmd EA. Fine Volume Slide Up

仅执行一次的音量上滑。

---

# Cmd EB. Fine Volume Slide Down

仅执行一次的音量下滑。

---

# Cmd EC. Cut Note

```text
Usage: $EC + cut tick
```

在指定 tick 切断音符。

实际上并非真正切断，而是将音量降为 0。

---

# Cmd ED. Note Delay

```text
Usage: $ED + delay ticks
```

延迟 note 到指定 tick 播放。

---

# Cmd EE. Pattern Delay

```text
Usage: $EE + delay count
```

延迟当前 pattern 若干 note。

期间其他效果仍然生效。

---

# Cmd EF. Invert Loop

```text
Usage: $EF + invert speed
```

需要较短 loop 才能工作（如 `$10,$20,$40,$80` 字节）。

逐字节反转 loop 数据。

---

# Cmd F. Set Speed

```text
Usage: $F + speed
```

设置歌曲播放速度。
