#pragma once

extern int g_quick_process, g_no_checkout, g_force_rebuild, g_dds_pause, g_test, g_no_compression, g_export_vrml;
extern char nvdxt_path[];

typedef enum GimmeErrorValue GimmeErrorValue;
typedef struct GroupDef GroupDef;

GimmeErrorValue checkoutSingleFile(const char *fname, const char *vrml_name);
GimmeErrorValue checkoutFiles(const char **fnames, const char *vrml_name);
void generateGroupBillboards(
		SA_PARAM_NN_VALID GroupDef* group,
        SA_PARAM_NN_STR const char* diffuseMapFname,
        SA_PARAM_NN_STR const char* normalMapFname,
        int numAngles, int numSeeds,
        Vec2 billboardSize, Vec2 billboardViewSize,
        float normalMapScale );

void setOutputFileDependancy( const char* sourceFile, const char** outputFiles );
char** getOutputFileDependants( const char* outputFname );
char** getSourceFileDependencies( char* srcFname );

int floorPower2( int value );
int ceilPower2( int value );
