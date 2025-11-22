// This file is not used by anything, I think!

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdtypes.h"
#include "utils.h"
#include "output.h"
#include "geo.h"
#include "file.h"
#include "error.h"
#include "token.h"
#include "assert.h"
#include "gimme.h"

#define GIMME_QUIET 0

//The following chunk is identical in seq.h and needs to be kept that way, why is it not #include seq.h?? 
//cuz of other includes...I will fix that when I get around to it by breaking seq.h up

#define MAX_FXNAMELEN 127
#define MAX_SEQFX 4

#define MAXMOVES 2048
#define MAXSTATES 448
#define STATE_ARRAY_SIZE ((MAXSTATES + 31)/32)
#define NAMELEN 32
#define ANIMNAMELEN 64 
#define MAX_CYCLE_MOVES 4
#define OUTDIR_LENGTH 128


typedef struct _SeqFx
{
	char	name[MAX_FXNAMELEN]; //pretty long
	U8		flags;
	F32		delay;
} SeqFx;

typedef struct
{
	char		name[NAMELEN];
	char		anim_name[ANIMNAMELEN];
	U32			requires[STATE_ARRAY_SIZE];
	U32			sets[STATE_ARRAY_SIZE];
	U32			setsonchild[STATE_ARRAY_SIZE];
	U32			interrupts[(MAXMOVES + 31)/32];
	short		start_frame;
	short		end_frame;
	void		*anim;
	short		next_move;
	U8			priority;  //unused till in game
	U8			interpolate;
	int			flags;
	F32			scale;
	SeqFx		seqfx[MAX_SEQFX];
	int			seqfx_count;
	short		cyclemove[MAX_CYCLE_MOVES];
	int			cyclemovecnt;
	void		* extra;
	U32			sticksonchild[STATE_ARRAY_SIZE];
	char		spam[4];
} SeqMove;

typedef struct
{
	int			move_count;		
	F32			root_height;	//how high to put ent's gfxroot.
	char		prefix[32];		//unused
	SeqMove		*moves;			//the .wrl's attached to each move in .seq 
	int			hash;			//name hash for in game management
	char		animfile[ANIMNAMELEN]; //file to look in for this character's anims
	int			spam[20];		//unused
} SeqInfo;

enum
{
	SEQMOVE_CYCLE			= (1 << 0),
	SEQMOVE_GLOBALANIM		= (1 << 1),
	SEQMOVE_FINISHCYCLE		= (1 << 2),
	SEQMOVE_REQINPUTS		= (1 << 3),
	SEQMOVE_COMPLEXCYCLE	= (1 << 4),
	SEQMOVE_NOINTERP		= (1 << 5),
};

enum
{
	SEQFX_CONSTANT			= (1 << 0),
};
//////////////////End the include thingy //////////////////




#define MAXGROUPS 256
typedef char NameStr[NAMELEN] ;

typedef struct
{
	NameStr	*names;
	int		count;
} NameArray;

typedef struct
{
	SeqMove move;
	NameArray	irq;
	char	next_move[NAMELEN];
	char	cyclemove_name[MAX_CYCLE_MOVES][NAMELEN];
} ParseMove;

ParseMove	parsemoves[MAXMOVES];
int			parsemove_count;

FILE *seq_file;
char seq_outdir[1000];
static int save_cmd = 0;
static SeqCmd seq_cmd;
static ParseMove	*curr;
static SeqFx * seqfx;
static char *last = 0;

#if 0
typedef struct
{
	char *name;
	int value;
} Token;

typedef struct
{
	int num;
	int count;
	char tokens[1000][80];
} SeqCmd;
#endif

Token moveflags_tokens[] =
{
	"Cycle",		SEQMOVE_CYCLE,		//Means play this over and over while it's getting the required inputs, but stop 
										//	as soon as the requires inputs stop.
	"DontLoad",		SEQMOVE_GLOBALANIM, 
	"FinishCycle",	SEQMOVE_FINISHCYCLE,//Means cycle, but when the inputs aren't valid, still play this anim to the end, 
										//  like a non-cycle anim.
	"ReqInputs",	SEQMOVE_REQINPUTS,  //Means only play this move while getting the required inputs, like cycle does
	"NoInterp",		SEQMOVE_NOINTERP,  //Means only play this move while getting the required inputs, like cycle does
	0,				0
};

Token fxflag_tokens[] =
{
	"Constant",		SEQFX_CONSTANT, //Means this fx doesn't die on it's own, it dies when you go into a move that doesn't call it
	0,				0,
};

//TO DO pass around these globals, cuz I get so confused......
Token gamestate_tokens[MAXSTATES]; 

Token		group_tokens[MAXGROUPS];
U32			group_members[MAXGROUPS][(MAXMOVES + 31)/32];
int			group_count;

Token		animname_tokens[MAXMOVES]; //rip out
int			animname_count;
enum
{
	SEQ_INFO = 1,
	SEQ_PREFIX,
	SEQ_ROOTHEIGHT,
	SEQ_OUTDIR,
	SEQ_GROUP,
	SEQ_SCALE,
	SEQ_FLAGS,
	SEQ_NEXTMOVE,
	SEQ_INTERPOLATE,
	SEQ_PRIORITY,
	SEQ_MOVE,
	SEQ_ANIM,
	SEQ_REQUIRES,
	SEQ_MEMBER,
	SEQ_INTERRUPTS,
	SEQ_SETS,
	SEQ_PLAYSOUND,
	SEQ_PLAYFX,
	SEQ_VOLUME,
	SEQ_DELAY,
	SEQ_INNER,
	SEQ_OUTER,
	SEQ_CYCLEMOVE,
	SEQ_ANIMFILE,
	SEQ_SETSONCHILD,
	SEQ_STICKSONCHILD,
	SEQ_FXFLAGS,
};

Token seq_tokens[] =
{
	{ "SeqInfo",				SEQ_INFO },
	{ "Prefix",					SEQ_PREFIX },
	{ "RootHeight",				SEQ_ROOTHEIGHT },
	{ "OutDir",					SEQ_OUTDIR },
	{ "Group",					SEQ_GROUP },
	{ "Scale",					SEQ_SCALE },
	{ "Flags",					SEQ_FLAGS },
	{ "NextMove",				SEQ_NEXTMOVE },
	{ "Interpolate",			SEQ_INTERPOLATE },
	{ "Priority",				SEQ_PRIORITY },
	{ "Move",					SEQ_MOVE },
	{ "Anim",					SEQ_ANIM },
	{ "Requires",				SEQ_REQUIRES },
	{ "Member",					SEQ_MEMBER },
	{ "Interrupts",				SEQ_INTERRUPTS },
	{ "Sets",					SEQ_SETS },
	{ "PlaySound",				SEQ_PLAYSOUND },
	{ "PlayFx",					SEQ_PLAYFX },
	{ "Volume",					SEQ_VOLUME },
	{ "Delay",					SEQ_DELAY },
	{ "FxFlags",				SEQ_FXFLAGS },
	{ "Inner",					SEQ_INNER },
	{ "Outer",					SEQ_OUTER },
	{ "CycleMove",				SEQ_CYCLEMOVE },
	{ "AnimFile",				SEQ_ANIMFILE },
	{ "SetsOnChild",			SEQ_SETSONCHILD },
	{ "SticksOnChild",			SEQ_STICKSONCHILD },
	{ 0,						0 },
};


static char *seqGetTok()
{
static char rdbuf[8000],linebuf[9000] = {0},*delim = " \t,";
char	*s = 0,*s2;

	if (last)
		s = strtok_r(0,delim,&last);
	if (s && *s == '#')
		s = 0;
	while (!s)
	{
		if (!fgets(rdbuf,sizeof(rdbuf),seq_file))
			return 0;
		for(s=rdbuf,s2=linebuf;*s;)
		{
			if (*s == '[' || *s == ']' || *s == '{' || *s == '}'|| *s == '#')
			{
				*s2++ = ' ';
				*s2++= *s++;
				*s2++ = ' ';
			}
			else
				*s2++ = *s++;
		}
		s2[-1] = 0;
		s = strtok_r(linebuf,delim,&last);
		if (s && *s == '#')
			s = 0;
	}
	return s;
}

static int cmdNum(char *s,Token *table)
{
int		i;

	if (!s)
		return 0;
	for(i=0;table[i].name;i++)
	{
		if (stricmp(s,table[i].name) == 0)
			return table[i].value;
	}
	return 0;
}


static void resetSeqCmd()
{
	save_cmd = 0;
	curr = 0;
	seqfx = 0;
	memset( &seq_cmd, 0, sizeof(SeqCmd));
	parsemove_count  = 0;
	last = 0;
}

static SeqCmd *getSeqCmd()
{
char	*s;
int		i;

	if (save_cmd)
		seq_cmd.num = save_cmd;
	else
		seq_cmd.num = cmdNum(seqGetTok(),seq_tokens);
	if (!seq_cmd.num)
		return 0;
	save_cmd = 0;
	for(i=0;;i++)
	{
		s = seqGetTok();
		if (!s)
			break;
		if ((save_cmd = cmdNum(s,seq_tokens)))
			break;
		strcpy(seq_cmd.tokens[i],s);
	}
	seq_cmd.count = i;
	return &seq_cmd;
}

static void addToGroup(char *name,int idx)
{
}

static void seqReadMove(SeqCmd *cmd)
{
int			i;

	switch(cmd->num)
	{
		case SEQ_MOVE:
			curr = &parsemoves[parsemove_count++];
			memset(&(curr->move), 0, sizeof(SeqMove)); //mm maybe redundant
			curr->move.interpolate = (U8)5;
			strcpy(curr->move.name,cmd->tokens[0]);
		xcase SEQ_ANIM:
			strcpy(curr->move.anim_name,cmd->tokens[0]);
			if (cmd->count > 1)
				curr->move.start_frame = atoi(cmd->tokens[1]);
			if (cmd->count > 2)
				curr->move.end_frame = atoi(cmd->tokens[2]);
		xcase SEQ_NEXTMOVE:
			strcpy(curr->next_move,cmd->tokens[0]);
		xcase SEQ_CYCLEMOVE:
			{ 
				strcpy( curr->cyclemove_name[curr->move.cyclemovecnt++], cmd->tokens[0] );
				assert( curr->move.cyclemovecnt <= MAX_CYCLE_MOVES );
			}
		xcase SEQ_INTERPOLATE:
			{
				int interpolate;
				interpolate = atoi(cmd->tokens[0]);
				curr->move.interpolate = (U8)interpolate;
			}
		xcase SEQ_PRIORITY:
			{
				curr->move.priority = (U8)( atoi(cmd->tokens[0]) );
			}
		xcase SEQ_SCALE:
			curr->move.scale = atof(cmd->tokens[0]);
		xcase SEQ_FLAGS:
			for(i=0;i<cmd->count;i++)
				curr->move.flags |= cmdNum(cmd->tokens[i],moveflags_tokens);
		xcase SEQ_REQUIRES:
			for(i=0;i<cmd->count;i++)
			{
				int bit = cmdNum(cmd->tokens[i],gamestate_tokens);
				if( bit <= 0 || bit >= MAXSTATES )
				{
					printf( "\nThere is bad requires bit in %s", curr->move.name );
					assert(0 == "Look at console of error description");
				}
				SETB(curr->move.requires,bit);
			}
		xcase SEQ_SETSONCHILD:
			for(i=0;i<cmd->count;i++)
			{
				int bit = cmdNum(cmd->tokens[i],gamestate_tokens);
				if( bit <= 0 || bit >= MAXSTATES )
				{
					printf( "\nThere is bad setsonchild bit in %s", curr->move.name );
					assert(0 == "Look at console of error description");
				}
				SETB(curr->move.setsonchild,cmdNum(cmd->tokens[i],gamestate_tokens));
			}
		xcase SEQ_STICKSONCHILD:
			for(i=0;i<cmd->count;i++)
			{
				int bit = cmdNum(cmd->tokens[i],gamestate_tokens);
				if( bit <= 0 || bit >= MAXSTATES )
				{
					printf( "\nThere is bad sticksonchild bit in %s", curr->move.name );
					assert(0 == "Look at console of error description");
				}
				SETB(curr->move.sticksonchild,cmdNum(cmd->tokens[i],gamestate_tokens));
			}
		xcase SEQ_SETS:
			for(i=0;i<cmd->count;i++)
			{
				int bit = cmdNum(cmd->tokens[i],gamestate_tokens);
				if( bit <= 0 || bit >= MAXSTATES )
				{
					printf( "\nThere is bad sets bit in %s", curr->move.name );
					assert(0 == "Look at console of error description");
				}
				SETB(curr->move.sets,cmdNum(cmd->tokens[i],gamestate_tokens));
			}
		xcase SEQ_INTERRUPTS:
			curr->irq.count = cmd->count;
			curr->irq.names = malloc(curr->irq.count * NAMELEN);
			for(i=0;i<cmd->count;i++)
				strcpy(curr->irq.names[i],cmd->tokens[i]);
		xcase SEQ_MEMBER:
			for(i=0;i<cmd->count;i++)
				SETB(group_members[cmdNum(cmd->tokens[i],group_tokens)],parsemove_count-1);
		xcase SEQ_PLAYSOUND:
			printf("Doing nothing with this");
		xcase SEQ_PLAYFX:
			seqfx = &curr->move.seqfx[curr->move.seqfx_count++];
			assert( strlen( cmd->tokens[0] ) < MAX_FXNAMELEN );
			fxCleanFileName(seqfx->name, cmd->tokens[0]);
		xcase SEQ_FXFLAGS:
			{
				int flags;
				for(i=0;i<cmd->count;i++)
					flags |= cmdNum(cmd->tokens[i],fxflag_tokens);
				assert( flags < (1 << 8 ) ); //since seqfx->flags is U8
				if(seqfx)
					seqfx->flags = (U8)flags;
			}
		xcase SEQ_VOLUME:
			printf("Doing nothing with this\n");
		xcase SEQ_DELAY:
			if (seqfx)
				seqfx->delay = atof(cmd->tokens[0]);
		xcase SEQ_INNER:
			printf("Doing nothing with this\n");
		xcase SEQ_OUTER:
			printf("Doing nothing with this\n");
	}
}

static void seqReadInfo(SeqCmd *cmd, SeqInfo * seq_info )
{
	switch(cmd->num)
	{
		case SEQ_PREFIX:
			strcpy(seq_info->prefix,cmd->tokens[0]);
		xcase SEQ_ROOTHEIGHT:
			seq_info->root_height = atof(cmd->tokens[0]);
		xcase SEQ_OUTDIR:
			strcpy( seq_outdir, cmd->tokens[0]);
		xcase SEQ_ANIMFILE:
			strcpy(  seq_info->animfile, cmd->tokens[0]);
	}
}

static void seqReadGroup(SeqCmd *cmd)
{
Token *tok;

	if (!group_count)
	{
		tok = &group_tokens[group_count++];
		tok->name = calloc(1,1);
	}
	tok = &group_tokens[group_count];
	tok->value = group_count++;
	strcpy((tok->name = malloc(strlen(cmd->tokens[0])+1)),cmd->tokens[0]);
}

/*adds to the list of animations this sequencer uses.  Used later to build the corresponding .geo file
*/
static void addAnimName( char *name )
{
Token *tok;

	if (cmdNum(name,animname_tokens))
		return;
	tok = &animname_tokens[animname_count];
	tok->value = ++animname_count;
	strcpy((tok->name = malloc(strlen(name)+1)),name);
	tok[1].name = 0;
}

/*Return true if all bits set in state are also set in test_state*/
static int areOnlyTheseBitsSet(U32 * state, U32 * test_state)
{
	int i;

	for(i=0;i<(MAXSTATES + 31)/32;i++)
	{
		if(state[i] & ~test_state[i])
			return 0;
	}
	return 1;
}

/*Sets whether or not this move should be predicted on the client.  The set_on_client
list is all the bits that are set on the client, and if a move is set only by some 
combination of those bits, it should be predictable on the client.*/
static void setPredictability(SeqMove * move)
{
	static int init = 0;
	static int bits_ok_to_predict[(MAXSTATES+31)/32]; 

	if(!init)
	{
		int i;
		//TO DO: duh
		static char set_on_client[100][128];
		int set_on_client_cnt = 0;
		FILE * file;
		//char cwd[128];
		char buf[500];
		char buffer[1024];

		//_getcwd(cwd, 128);
		//sprintf(buf, "N:\\game\\src\\player_library\\animations\\predictable_bits.txt"); //TO DO: duh
		sprintf(buf, "C:\\game\\src\\player_library\\animations\\predictable_bits.txt"); //TO DO: duh
		//gimmeMakeLocalNameFromRel(GIMME_DB_SRC, "/player_library/animations/predictable_bits.txt", buf);
		file = fileOpen(buf,"rt");
		if (file)
		{
			int		go = 1;
			int len;

			while(go)
			{
				fgets(buffer,sizeof(buffer),file);

				if(strlen(buffer) > 1)
				{
					len = strlen(buffer) - 1;
					if (len >= 0 && buffer[len] == '\n')
						buffer[len] = 0;
					strcpy(set_on_client[set_on_client_cnt], buffer);
					set_on_client_cnt++;
				}
				else
				{
					go = 0;
				}
			}
			set_on_client[set_on_client_cnt][0] = '0';
			fclose(file);
		}
			
		memset(bits_ok_to_predict, 0, sizeof(bits_ok_to_predict));
			
		for(i = 0; set_on_client[i][0] != '0' ; i++)
		{
			int bit = cmdNum(set_on_client[i],gamestate_tokens);
			assert( bit > 0 && bit < MAXSTATES );
			SETB( bits_ok_to_predict, bit );
		}
		init = 1;
	}
	
	if( areOnlyTheseBitsSet(move->requires, bits_ok_to_predict) )
	{
		SETB(move->sets, cmdNum("Predictable", gamestate_tokens) );
	}
	else
	{
		CLRB(move->sets, cmdNum("Predictable", gamestate_tokens) );
	}
}

static int countBitsU32(U32 *data,int numwords)
{
int		i,bcount;

	for(bcount=i=0;i<numwords*32;i++)
	{
		if (TSTB(data,i))
			bcount++;
	}
	return bcount;
}

static void setPriority(SeqMove * move)
{
	if(!move->priority)
		move->priority = countBitsU32(move->requires,(MAXSTATES + 31)/32);
}

static void setScale(SeqMove * move)
{
	if (!move->scale) 
		move->scale = 1.0;
}

static void finishMoveSetup(ParseMove *curr)
{
int		i,j,k,idx,wcount, found;
char	*name;

	
	wcount = ARRAY_SIZE(curr->move.interrupts);
	for(i=0;i<curr->irq.count;i++)
	{
		name = curr->irq.names[i];
		idx = cmdNum(name,group_tokens);
		if (idx)
		{
			for(j=0;j<wcount;j++)
				curr->move.interrupts[j] |= group_members[idx][j];
			continue;
		}
		for(j=0;j<parsemove_count;j++)
		{
			if (strcmp(name,parsemoves[j].move.name) == 0)
			{
				SETB(curr->move.interrupts,j);
				break;
			}
		}
	}

	//Get idx of the nextmove from the nextmove string name
	curr->move.next_move = 0;  //next move is ready by default...
	for(j=0;j<parsemove_count;j++)
	{
		if (strcmp(curr->next_move,parsemoves[j].move.name) == 0)
		{
			curr->move.next_move = j;
			break;
		}
	}

	//Set up the move's cyclemoves
	assert( curr->move.cyclemovecnt <= MAX_CYCLE_MOVES );
	if( curr->move.cyclemovecnt )
		curr->move.flags |= SEQMOVE_COMPLEXCYCLE;

	for( j = 0 ; j < curr->move.cyclemovecnt ; j++ )
	{
		found = 0;
		for( k = 0 ; k < parsemove_count ; k++ )
		{
			if ( strcmp( curr->cyclemove_name[j], parsemoves[k].move.name ) == 0)
			{
				curr->move.cyclemove[j] = k;
				found = 1;
				break;
			}
		}
		if(!found)
		{
			printf("The move %s is tagged as having a complex cycle move which doesn't exist %s", curr->move.name, curr->cyclemove_name[j]); 
			assert(0 == "See window");
		}
	}

	parseAnimNameFromFileName(curr->move.anim_name, curr->move.anim_name);

	addAnimName(curr->move.anim_name); //unused

	setPredictability(&curr->move);
	setPriority(&curr->move);
	setScale(&curr->move);
}

static void seqWrite(char *fname, SeqInfo * seq_info)
{
	FILE	*file;
	int		i;
	extern int no_check_out;

	if (!no_check_out)
		gimmeCheckout(fname, GIMME_CHECKOUT, GIMME_QUIET);
	file = fopen(fname,"wb");
	if (!file)
		FatalErrorf("Can't open %s for writing!\n",fname);
	
	fwrite(seq_info,sizeof(SeqInfo),1,file);
	for(i=0;i<parsemove_count;i++)
	{
		assert(parsemoves[i].move.anim_name);
		fwrite(&parsemoves[i].move,sizeof(SeqMove),1,file);
	}
	fclose(file);
	//gimmeCheckout(fname, GIMME_CHECKIN, GIMME_QUIET);
}

static void seqRead(char *fname, SeqInfo * seq_info)
{
SeqCmd	*cmd;
int		i,curr_type = -1;
int		major_types[] = { SEQ_INFO,	SEQ_MOVE, SEQ_GROUP, 0 };

	seq_file = fopen(fname,"rt");
	if (!seq_file)
		FatalErrorf("Can't open %s for reading!\n",fname);
	resetSeqCmd();
	for(;;)
	{
		cmd = getSeqCmd();
		if (!cmd)
			break;
		for(i=0;major_types[i];i++)
		{
			if (cmd->num == major_types[i])
				curr_type = cmd->num;
		}
		switch(curr_type)
		{
			case SEQ_INFO:
				seqReadInfo(cmd, seq_info);
			xcase SEQ_MOVE:
				seqReadMove(cmd);
			xcase SEQ_GROUP:
				seqReadGroup(cmd);
			break;
			default:
				FatalErrorf("parse error at line %d\n",0);
		}
	}
	fclose(seq_file);

	seq_info->move_count = parsemove_count;

	for(i=0;i<parsemove_count;i++)   //All moves are read in so you can do whatever you want.
		finishMoveSetup(&parsemoves[i]);
}

/*Unused Uses animname_tokens list to write a .geo file of all used animations
*/
static void seqMakeAnims(char *fnametarget, Token animname_tokens[], int animname_count)
{
	char	 buf[1000];
	int		i;
	extern int no_check_out;

	for(i=0;i<animname_count;i++)
	{
		sprintf(buf,"%s.wrl", animname_tokens[i].name);
		geoAddFile( buf, NO_MERGE_NODES, PROCESS_ANIM_ONLY );
	}
	if (!no_check_out)
		gimmeCheckout(fnametarget, GIMME_CHECKOUT, GIMME_QUIET);
	outputData(fnametarget);
	//gimmeCheckout(fnametarget, GIMME_CHECKIN, GIMME_QUIET);
	//outputResetVars();
}

/*Make sure all animations referenced are in fact in the .geos given*/
static void	seqCheckExistanceOfAnims( ParseMove	parsemoves[], int parsemove_count, SeqInfo * seq_info )
{
	int i, j, found, all_found = 1;
	AnimationSet * animset;
	char animfile[128];

	printf("\nThis sequencer gets anims from: %s\n", seq_info->animfile);
	animset = animGetSet( seq_info->animfile, LOAD_ALL );
	if(!animset)
		printf("Can't find .skel file %s\n", animfile);
	else
	{
		for(i = 0 ; i < parsemove_count ; i++ )
		{
			found = 0;
			for(j = 0 ; j < animset->skeleton_count ; j++ )
			{
				if(!stricmp( parsemoves[i].move.anim_name, animset->skeleton_anims[j].name ) )
				{
					found = 1;
					break;
				}
			}
			if(!found)
			{
				all_found = 0;
				printf("Can't find %s in %s. ", parsemoves[i].move.anim_name, animset->name);
				printf("\t(used by %s)\n", parsemoves[i].move.name );
			}
		}
	}

	if( !all_found || !animset )
	{
		char dummy[10]; 
		printf("\n(Game will crash if these moves can't find their animation.) \nHit enter to continue\n");
		gets( dummy );
	}

}

/*parse the base name of this sequencer and the seq_outdir, and get the output file path
*/
static void seqBuildFullTargetPath(char * fulltargetpath, char * seq_outdir, char * fname)
{
	char * s;
	char basename[100];

	//2. Generate full target path
	//parse name ("blah") then write to 'data/seq_outdir/blah.seq' (seq_outdir is given in blah.txt)
	s = strrchr(fname, '/');
	if(!s)
		s = strrchr(fname, '\\');
	if(!s)
		FatalErrorf("didn't get a proper file name %s", fname);
	strcpy(basename, s+1);
	s = strrchr(basename,'.');
	if (s)
		*s = 0;

	sprintf( fulltargetpath,"%s/%s/%s.seq", fileDataDir(), seq_outdir, basename );
	//typically "N:/game/data/player_library/blah.seq"
}

/*Only entrance to seq.c except seqLoadStates, which only kinda counts, and there is no exit...
Soon seqMakeAnims will either be a new way in or out or get bypassed/moved out entirely...
*/
void seqProcess( char *inputfname, char * outputfname )
{
SeqInfo seq_info;
//char	fulltargetpath[512];

	seqRead(inputfname, &seq_info); //fname = full source path: "CWD/whereever(usually blah)/blah.txt"
	seqCheckExistanceOfAnims( parsemoves, parsemove_count, &seq_info  ); //debug 
	
	//seqBuildFullTargetPath(fulltargetpath, seq_outdir, fname); //get full output path
	strcpy( seq_outdir, outputfname ); //doesn't appear used

	//TO DO this is clunky, but I don't ha
	seqWrite( outputfname, &seq_info );

	parsemove_count = 0;
	memset(parsemoves, 0 , sizeof(ParseMove) * MAXMOVES );
	memset(group_tokens, 0 , sizeof(Token) * MAXGROUPS );
	memset(group_members, 0 , sizeof(U32) * MAXGROUPS * ((MAXMOVES + 31)/32) );
	group_count = 0;
	memset(animname_tokens, 0 , sizeof(Token) * MAXMOVES );
	animname_count = 0;
}


enum
{
	ST_INPUT = 1,
	ST_OUTPUT,
};

Token state_tokens[] =
{
	{ "Input",		ST_INPUT },
	{ "Output",		ST_OUTPUT },
	{ 0,			0 },
};

#include "stdarg.h"
void safe_fprintf(FILE *file,char const *fmt, ...)
{
char str[1000];
va_list ap;

	if (!file)
		return;
	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);
	fprintf(file,"%s",str);
}


void seqLoadStates()
{
char	*s;
int		i=1,j;
FILE	*out_files[2],*out_h,*out_c;
char	*seqfname	= "anims/seqstates.txt";
char	cnames[2][128]		= { "c:/src/common/seq/seqstate.h",
								"c:/src/common/seq/seqstate.c" };
char	*tempname	= "c:/seqtemp.c";
char	buf[100];
SeqCmd	*cmd;
U32		clr_bits[(MAXSTATES+31)/32];
CmdState tok_state;

	memset(clr_bits,255,sizeof(clr_bits));
	seq_file = fileOpen(seqfname,"rt");
	if (!seq_file)
		FatalErrorf("Can't open %s for reading",seqfname);

	for(j=0;j<2;j++)
	{
		out_files[j] = fopen(cnames[j],"wt");
/*		if (!out_files[j])
		{
			cnames[j][0] = 'd';
			out_files[j] = fopen(cnames[j],"wt");
		}
*/	}
	out_h = out_files[0];
	out_c = out_files[1];

	tokenParseInit(&tok_state);
	//tok_state = tokenParseAlloc();
	safe_fprintf(out_h,"// Created by getvrml - DO NOT EDIT\n");
	safe_fprintf(out_h,"#ifndef _SEQSTATE_H\n");
	safe_fprintf(out_h,"#define _SEQSTATE_H\n\n");

	safe_fprintf(out_c,"char* SeqStateNames[] = {\n\"BLANK_STATE\",\n");
	while(cmd = tokenParse(seq_file,state_tokens,&tok_state))
	{
		for(s = cmd->tokens[0];*s;s++)
			*s = toupper(*s);
		s = cmd->tokens[0];
		gamestate_tokens[i-1].name = strcpy(malloc(strlen(s)+1),s);
		gamestate_tokens[i-1].value = i;
		if (cmd->num == ST_INPUT)
			SETB(clr_bits,i);
		else
			CLRB(clr_bits,i);
		safe_fprintf(out_c,"\"%s\",\n",s);
		sprintf(buf,"%-22.22s",s);
		safe_fprintf(out_h,"#define STATE_%s %d\n",buf,i);
		i++;
	}
	safe_fprintf(out_h,"#define STATE_COUNT %d\n",i);
	gamestate_tokens[i-1].name = 0;
	gamestate_tokens[i-1].value = 0;
	fclose(seq_file);
	seq_file = 0;
	safe_fprintf(out_c,"};\n");

	safe_fprintf(out_h,"\n");
	for(i=0;i<(MAXSTATES+31)/32;i++)
	{
		safe_fprintf(out_h,"#define STATE_CLEAR%d 0x%08x\n",i,clr_bits[i]);
	}
	safe_fprintf(out_h,"\n\nextern char* SeqStateNames[];\n\n#endif\n");

	//tokenParseFree(tok_state);

	for(i=0;i<2;i++)
	{
		if (out_files[i])
			fclose(out_files[i]);
	}
}
