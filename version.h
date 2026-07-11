#ifndef BTREGKEY_VERSION_H
#define BTREGKEY_VERSION_H

// The single source of truth for the product version. Bump these three numbers
// and everything else -- the console banner and the PE version resource
// (btregkey.rc) -- follows automatically.
#define BTREGKEY_VER_MAJOR 0
#define BTREGKEY_VER_MINOR 1
#define BTREGKEY_VER_PATCH 0

// "0.1.0" as a narrow string literal, built from the numbers above. Used by the
// .rc version resource (and safe for the resource compiler).
#define BK_STR2(x) #x
#define BK_STR(x)  BK_STR2(x)
#define BTREGKEY_VERSION_STR \
	BK_STR(BTREGKEY_VER_MAJOR) "." BK_STR(BTREGKEY_VER_MINOR) "." BK_STR(BTREGKEY_VER_PATCH)

#ifndef RC_INVOKED
// L"0.1.0" for the (Unicode) console. TEXT() cannot wrap the pieced-together
// macro, so widen each component and let adjacent wide literals concatenate.
#define BK_WIDEN2(x) L##x
#define BK_WIDEN(x)  BK_WIDEN2(x)
#define BTREGKEY_VERSION_WSTR \
	BK_WIDEN(BK_STR(BTREGKEY_VER_MAJOR)) L"." \
	BK_WIDEN(BK_STR(BTREGKEY_VER_MINOR)) L"." \
	BK_WIDEN(BK_STR(BTREGKEY_VER_PATCH))
#endif // RC_INVOKED

#endif // BTREGKEY_VERSION_H
