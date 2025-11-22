#pragma once

typedef struct DisplayMessageWithVO DisplayMessageWithVO;

/// Make .vo.txt files for each supported language.
///
/// Exists to be run by the CB.
void gslMakeVOTXTFiles( void );

/// Given a message key, get the associated audio event (if any).
const char* gslVOGetAudioEvent( const DisplayMessageWithVO* msg );

/// List all audio assets referenced in .vo files.
void gslVOGetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);
