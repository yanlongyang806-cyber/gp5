#pragma once

typedef struct AccountStub AccountStub;
typedef struct AccountStub_AutoGen_NoConst AccountStub_AutoGen_NoConst;

void InitializeAccountStub(NOCONST(AccountStub) *lhs);
void MergeTwoAccountStubs(NOCONST(AccountStub) *lhs, AccountStub *rhs);
