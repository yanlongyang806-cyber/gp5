#pragma once

// Read a string from the product-specific registry key
extern int readRegStr(const char *prodName, const char *name, char *outbuffer, int outlen, bool useHistory);

// Write a string to the product-specific registry key
extern int writeRegStr(const char *prodName, const char *name, const char *str);

// Read a number from the product-specific registry key
extern int readRegInt(const char *prodName, const char *name, unsigned int *out, bool useHistory);

// Write a number to the product-specific registry key
extern int writeRegInt(const char *prodName, const char *name, unsigned int val);

extern bool RegistryBackedStrSet(const char *productName, const char *keyName, const char *value, bool bUseHistory);
// returns true if there was a value in the registry (different than default), false if default utilized
extern bool RegistryBackedStrGet(const char *productName, const char *keyName, char *value, int valueMaxLength, bool bUseHistory); // set default in value before call

extern bool RegistryBackedIntSet(const char *productName, const char *keyName, int value, bool bUseHistory);
// returns true if there was a value in the registry (different than default), false if default utilized
extern bool RegistryBackedIntGet(const char *productName, const char *keyName, int *pValue, bool bUseHistory); // set default in value before call

extern bool RegistryBackedBoolSet(const char *productName, const char *keyName, bool bValue, bool bUseHistory);
// returns true if there was a value in the registry (different than default), false if default utilized
extern bool RegistryBackedBoolGet(const char *productName, const char *keyName, bool *pbDefaultValue, bool bUseHistory); // set default in value before call

