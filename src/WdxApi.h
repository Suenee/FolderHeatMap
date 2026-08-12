#pragma once

#include <windows.h>

// Minimal subset of Total Commander WDX SDK 2.12 used by FolderHeatMap.
// Source API reference: ghisler/WDX-SDK src/contplug.h

#define ft_nomorefields 0
#define ft_numeric_32 1
#define ft_numeric_64 2
#define ft_numeric_floating 3
#define ft_date 4
#define ft_time 5
#define ft_boolean 6
#define ft_multiplechoice 7
#define ft_string 8
#define ft_fulltext 9
#define ft_datetime 10
#define ft_stringw 11
#define ft_fulltextw 12

#define ft_nosuchfield -1
#define ft_fileerror -2
#define ft_fieldempty -3
#define ft_ondemand -4
#define ft_notsupported -5
#define ft_delayed 0

#define contst_readnewdir 1
#define contst_refreshpressed 2
#define contst_showhint 4

#define CONTENT_DELAYIFSLOW 1
#define CONTENT_PASSTHROUGH 2
