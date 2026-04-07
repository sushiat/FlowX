/**
 * @file FlowX.h
 * @brief MFC application class declaration for the FlowX DLL.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#ifndef __AFXWIN_H__
#error "include 'pch.h' before including this file for PCH"
#endif
#include "resource.h"

/// @brief MFC application class for the FlowX DLL; initialises the MFC runtime for the plugin.
class CFlowXApp : public CWinApp
{
  public:
    /// @brief Constructs the MFC application object.
    CFlowXApp();

    /// @brief Called by MFC when the DLL is loaded; performs one-time initialisation.
    /// @return TRUE if initialisation succeeded.
    virtual BOOL InitInstance();

    DECLARE_MESSAGE_MAP()
};
