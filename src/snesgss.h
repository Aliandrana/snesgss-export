//---------------------------------------------------------------------------

// ::TODO LICENSE, SOURCE::

#ifndef _SNESGSS_EXPORT_H_
#define _SNESGSS_EXPORT_H_

//---------------------------------------------------------------------------
#include <stdio.h>
#include <string>

//---------------------------------------------------------------------------

#define MAX_INSTRUMENTS		99
#define MAX_SONGS		99
#define MAX_ROWS		10000

#define DEFAULT_PAGE_ROWS	16

struct instrumentStruct {
	short *source;
	int source_length;
	int source_rate;
	int source_volume;

	int env_ar;
	int env_dr;
	int env_sl;
	int env_sr;

	int length;

	int loop_start;
	int loop_end;
	bool loop_enable;
	bool loop_unroll;

	int wav_loop_start;
	int wav_loop_end;

	int resample_type;
	int downsample_factor;
	bool ramp_enable;

	int BRR_adr;
	int BRR_size;

	int eq_low;
	int eq_mid;
	int eq_high;

	std::string name;
};

struct noteFieldStruct {
	unsigned char note;
	unsigned char instrument;
	unsigned char effect;
	unsigned char value;
	unsigned char volume;
};

struct rowStruct {
	int speed;
	bool marker;
	std::string name;
	noteFieldStruct chn[8];
};

struct songStruct {
	rowStruct row[MAX_ROWS];

	int length;
	int loop_start;
	int measure;

	bool effect;

	int compiled_size;

	std::string name;
};


//---------------------------------------------------------------------------
class SnesGssExporter {
public:
	SnesGssExporter();
	~SnesGssExporter();

public:

	void CleanupSongs();
	int CleanupInstruments();

	void ExportSPC(int songId, std::string filename);
	void Export(std::string dirname);
	void InstrumentExtractSourceWave(int instrumentId, std::string filename);

	void PrintMemoryUse();
	bool ModuleOpen(std::string filename);
	bool ModuleSave(std::string filename);

private:
	void BRREncode(int ins);

	int SamplesCompile(unsigned int adr,int one_sample);

	bool SongIsChannelEmpty(songStruct *s,int chn);
	bool SongIsRowEmpty(songStruct *s,int row,bool marker);
	bool SongIsEmpty(songStruct *s);
	int SongFindLastRow(songStruct *s);
	void SongCleanUp(songStruct *s);

	int ChannelCompile(songStruct *s,int chn,int start_row,int compile_adr,int &play_adr);
	int ChannelCompress(int compile_adr,int &play_adr,int loop_adr,int src_size,int ref_max);
	void ChannelCompressFlush(int compile_adr);
	int SongCompile(songStruct *s,int row,int adr,bool mute);
	int EffectsCompile(int start_adr);
	int DelayCompile(int adr,int delay);

	bool SPCCompile(songStruct *s,int start_row,bool mute,bool effects,int one_sample);

	void ModuleClear(void);
	bool ModuleOpenFile(std::string filename);

	void InstrumentClear(int ins);

	void InstrumentDataWrite(FILE *file,int id,int ins);
	void InstrumentDataParse(int id,int ins);

	void ResetSongStruct(songStruct *song);

	void SwapInstrumentNumberAll(int ins1,int ins2);

	char*  MakeNameForAlias(std::string name);
	bool ExportAll(std::string dir);

	void CompileAllSongs(void);
	int InsCalculateBRRSize(int ins,bool loop_only);

	int SongCalculateDuration(int song);
	float get_volume_scale(int vol);

private:
	instrumentStruct insList[MAX_INSTRUMENTS];

	unsigned char *BRRTemp;
	int BRRTempAllocSize;
	int BRRTempSize;
	int BRRTempLoop;

	unsigned char SPCTemp[66048];
	unsigned char SPCTempPlay[66048];
	unsigned char SPCMem[65536];

	unsigned char SPCChnMem[65536];
	unsigned char SPCChnPack[65536];

	unsigned char CompressSeqBuf[256];

	int CompressSrcPtr;
	int CompressSeqPtr;
	int CompressOutPtr;

	songStruct songList[MAX_SONGS];
	songStruct tempSong;

	bool ChannelMute[8];

	int SPCInstrumentsSize;
	int SPCMusicSize;
	int SPCEffectsSize;
	int SPCMemTopAddr;
	int SPCMusicLargestSize;
	int InstrumentRenumberList[MAX_INSTRUMENTS];
	int InstrumentsCount;

	bool UpdateSampleData;
};
//---------------------------------------------------------------------------
#endif

