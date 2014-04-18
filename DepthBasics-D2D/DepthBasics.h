//------------------------------------------------------------------------------
// <copyright file="DepthBasics.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#pragma once

#include "resource.h"
#include "NuiApi.h"
#include "ImageRenderer.h"

#define MAX_PLAYER_INDEX    6

enum DEPTH_TREATMENT
{
	CLAMP_UNRELIABLE_DEPTHS,
	TINT_UNRELIABLE_DEPTHS,
	DISPLAY_ALL_DEPTHS,
};

class CDepthBasics
{
    static const int        cDepthWidth  = 640;
    static const int        cDepthHeight = 480;
    static const int        cBytesPerPixel = 4;

    static const int        cStatusMessageMaxLen = MAX_PATH*2;

public:
    /// <summary>
    /// Constructor
    /// </summary>
    CDepthBasics();

    /// <summary>
    /// Destructor
    /// </summary>
    ~CDepthBasics();

    /// <summary>
    /// Handles window messages, passes most to the class instance to handle
    /// </summary>
    /// <param name="hWnd">window message is for</param>
    /// <param name="uMsg">message</param>
    /// <param name="wParam">message data</param>
    /// <param name="lParam">additional message data</param>
    /// <returns>result of message processing</returns>
    static LRESULT CALLBACK MessageRouter(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// Handle windows messages for a class instance
    /// </summary>
    /// <param name="hWnd">window message is for</param>
    /// <param name="uMsg">message</param>
    /// <param name="wParam">message data</param>
    /// <param name="lParam">additional message data</param>
    /// <returns>result of message processing</returns>
    LRESULT CALLBACK        DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// Creates the main window and begins processing
    /// </summary>
    /// <param name="hInstance"></param>
    /// <param name="nCmdShow"></param>
    int                     Run(HINSTANCE hInstance, int nCmdShow);

private:
    HWND                    m_hWnd;

    bool                    m_bNearMode;

    // Current Kinect
    INuiSensor*             m_pNuiSensor;

    // Direct2D
    ImageRenderer*          m_pDrawDepth;
    ID2D1Factory*           m_pD2DFactory;
    
    HANDLE                  m_pDepthStreamHandle;
    HANDLE                  m_hNextDepthFrameEvent;

    BYTE*                   m_depthRGBX;
	///////////////////////////////////
	//Code from NuiImageBuffer
	DEPTH_TREATMENT     m_depthTreatment;


	bool m_nearMode;
	/// <summary>
	/// Initialize the depth-color mapping table.
	/// </summary>
	void InitDepthColorTable();

	/// <summary>
	/// Calculate intensity of a certain depth
	/// </summary>
	/// <param name="depth">A certain depth</param>
	/// <returns>Intensity calculated from a certain depth</returns>
	BYTE GetIntensity(int depth);

	/// <summary>
	/// Set color value
	/// </summary>
	/// <param name="pColor">The pointer to the variable to be set with color</param>
	/// <param name="red">Red component of the color</param>
	/// <param name="green">Green component of the color</parma>
	/// <param name="blue">Blue component of the color</param>
	/// <param name="alpha">Alpha component of the color</param>
	inline void SetColor(UINT* pColor, BYTE red, BYTE green, BYTE blue, BYTE alpha = 255);
	//////////////////////////////////

    /// <summary>
    /// Main processing function
    /// </summary>
    void                    Update();

    /// <summary>
    /// Create the first connected Kinect found 
    /// </summary>
    /// <returns>S_OK on success, otherwise failure code</returns>
    HRESULT                 CreateFirstConnected();

    /// <summary>
    /// Handle new depth data
    /// </summary>
    void                    ProcessDepth();

    /// <summary>
    /// Set the status bar message
    /// </summary>
    /// <param name="szMessage">message to display</param>
    void                    SetStatusMessage(WCHAR* szMessage);

private:
	static const BYTE    m_intensityShiftR[MAX_PLAYER_INDEX + 1];
	static const BYTE    m_intensityShiftG[MAX_PLAYER_INDEX + 1];
	static const BYTE    m_intensityShiftB[MAX_PLAYER_INDEX + 1];
	UINT m_depthColorTable[MAX_PLAYER_INDEX + 1][USHRT_MAX + 1];
};
