// DelHelX.h : main header file for the DelHelX DLL
//

#pragma once

#ifndef __AFXWIN_H__
    #error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"       // main symbols


// CDelHelApp
// See DelHelX.cpp for the implementation of this class
//

/// @brief MFC application class for the DelHelX DLL; initialises the MFC runtime for the plugin.
class CDelHelXApp : public CWinApp
{
public:
    /// @brief Constructs the MFC application object.
    CDelHelXApp();

// Overrides
public:
    /// @brief Called by MFC when the DLL is loaded; performs one-time initialisation.
    /// @return TRUE if initialisation succeeded.
    virtual BOOL InitInstance();

    DECLARE_MESSAGE_MAP()
};
