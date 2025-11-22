#ifndef _OUTPUT_H
#define _OUTPUT_H

typedef struct Node Node;
typedef struct AltPivotInfo AltPivotInfo;

void outputPackAllNodes(const char *little_fname/*, const char *big_fname*/, const char *deps_fname, const char *name, Node **list, int count);
int writeModelHeaderToFile(const char *fname, AltPivotInfo*** apis, Node*** meshlist, bool no_checkout, bool is_core, bool character_lib);
void outputToVrml(const char *output_filename, Node **list, int count);

#endif
