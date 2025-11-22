#pragma once



AUTO_ENUM;
typedef enum BuilderPlexerActionType
{
	BPACTION_DONOTHING,
	BPACTION_DISABLEACTIVE,
	BPACTION_ACTIVATE,
} BuilderPlexerActionType;

AUTO_STRUCT;
typedef struct BuilderPlexerAction
{
	BuilderPlexerActionType eType;
	BuilderPlexerChoice *pChoice; AST(UNOWNED)
	char *pName; AST(ESTRING)
	char *pDescription; AST(ESTRING)
} BuilderPlexerAction;


void DeviseActions(void);
BuilderPlexerAction *ChooseAction(void);
void DoAction(BuilderPlexerAction *pAction);
