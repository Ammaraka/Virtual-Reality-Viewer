/** 
 * @file llpanellogin.cpp
 * @brief Login dialog and logo display
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 * 
 * Copyright (c) 2002-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llpanellogin.h"

#include "llpanelgeneral.h"

#include "hippogridmanager.h"
#include "hippolimits.h"
#include "floatergridmanager.h"

#include "indra_constants.h"		// for key and mask constants
#include "llfontgl.h"
#include "llmd5.h"
#include "llsecondlifeurls.h"
#include "v4color.h"

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcommandhandler.h"		// for secondlife:///app/login/
#include "llcombobox.h"
#include "llcurl.h"
#include "llviewercontrol.h"
#include "llfirstuse.h"
#include "llfloaterabout.h"
#include "llfloatertest.h"
#include "llfloaterpreference.h"
#include "llfocusmgr.h"
#include "lllineeditor.h"
#include "llstartup.h"
#include "lltextbox.h"
#include "llui.h"
#include "lluiconstants.h"
#include "llurlsimstring.h"
#include "llviewerimagelist.h"
#include "llviewermenu.h"			// for handle_preferences()
#include "llviewernetwork.h"
#include "llviewerwindow.h"			// to link into child list
#include "llnotify.h"
#include "llappviewer.h"					// for gHideLinks
#include "llurlsimstring.h"
#include "lluictrlfactory.h"
#include "llhttpclient.h"
#include "llweb.h"
#include "viewerversion.h"
#include "llmediactrl.h"

#include "llfloatermediabrowser.h"
#include "llfloatertos.h"

#include "llglheaders.h"

// [RLVa:KB]
#include "rlvhandler.h"
// [/RLVa:KB]

#define USE_VIEWER_AUTH 0

const S32 BLACK_BORDER_HEIGHT = 160;
const S32 MAX_PASSWORD = 16;

LLPanelLogin *LLPanelLogin::sInstance = NULL;
BOOL LLPanelLogin::sCapslockDidNotification = FALSE;


class LLLoginRefreshHandler : public LLCommandHandler
{
public:
	// don't allow from external browsers
	LLLoginRefreshHandler() : LLCommandHandler("login_refresh", true) { }
	bool handle(const LLSD& tokens, const LLSD& query_map, LLMediaCtrl* web)
	{	
		if (LLStartUp::getStartupState() < STATE_LOGIN_CLEANUP)
		{
			LLPanelLogin::loadLoginPage();
		}	
		return true;
	}
};

LLLoginRefreshHandler gLoginRefreshHandler;



// helper class that trys to download a URL from a web site and calls a method 
// on parent class indicating if the web server is working or not
class LLIamHereLogin : public LLHTTPClient::Responder
{
	private:
		LLIamHereLogin( LLPanelLogin* parent ) :
		   mParent( parent )
		{}

		LLPanelLogin* mParent;

	public:
		static boost::intrusive_ptr< LLIamHereLogin > build( LLPanelLogin* parent )
		{
			return boost::intrusive_ptr< LLIamHereLogin >( new LLIamHereLogin( parent ) );
		};

		virtual void  setParent( LLPanelLogin* parentIn )
		{
			mParent = parentIn;
		};

		// We don't actually expect LLSD back, so need to override completedRaw
		virtual void completedRaw(U32 status, const std::string& reason,
								  const LLChannelDescriptors& channels,
								  const LLIOPipe::buffer_ptr_t& buffer)
		{
			completed(status, reason, LLSD()); // will call result() or error()
		}
	
		virtual void result( const LLSD& content )
		{
			if ( mParent )
				mParent->setSiteIsAlive( true );
		};

		virtual void error( U32 status, const std::string& reason )
		{
			if ( mParent )
				mParent->setSiteIsAlive( false );
		};
};

// this is global and not a class member to keep crud out of the header file
namespace {
	boost::intrusive_ptr< LLIamHereLogin > gResponsePtr = 0;
};

//---------------------------------------------------------------------------
// Public methods
//---------------------------------------------------------------------------
LLPanelLogin::LLPanelLogin(const LLRect &rect,
						 BOOL show_server,
						 void (*callback)(S32 option, void* user_data),
						 void *cb_data)
:	LLPanel(std::string("panel_login"), LLRect(0,600,800,0), FALSE),		// not bordered
	mLogoImage(),
	mCallback(callback),
	mCallbackData(cb_data),
	mHtmlAvailable( TRUE )
{
	setFocusRoot(TRUE);

	setBackgroundVisible(FALSE);
	setBackgroundOpaque(TRUE);

	// instance management
	if (LLPanelLogin::sInstance)
	{
		llwarns << "Duplicate instance of login view deleted" << llendl;
		delete LLPanelLogin::sInstance;

		// Don't leave bad pointer in gFocusMgr
		gFocusMgr.setDefaultKeyboardFocus(NULL);
	}

	LLPanelLogin::sInstance = this;

	// add to front so we are the bottom-most child
	gViewerWindow->getRootView()->addChildAtEnd(this);

	// Logo
	mLogoImage = LLUI::getUIImage("startup_logo.j2c");

	LLUICtrlFactory::getInstance()->buildPanel(this, "panel_login.xml");
	
#if USE_VIEWER_AUTH
	//leave room for the login menu bar
	setRect(LLRect(0, rect.getHeight()-18, rect.getWidth(), 0)); 
#endif
	reshape(rect.getWidth(), rect.getHeight());

#if !USE_VIEWER_AUTH
	childSetPrevalidate("first_name_edit", LLLineEditor::prevalidatePrintableNoSpace);
	childSetPrevalidate("last_name_edit", LLLineEditor::prevalidatePrintableNoSpace);

	childSetCommitCallback("password_edit", mungePassword);
	childSetKeystrokeCallback("password_edit", onPassKey, this);
	childSetUserData("password_edit", this);

	// change z sort of clickable text to be behind buttons
	sendChildToBack(getChildView("channel_text"));
	sendChildToBack(getChildView("forgot_password_text"));

	LLLineEditor* edit = getChild<LLLineEditor>("password_edit");
	if (edit) edit->setDrawAsterixes(TRUE);

	LLComboBox* combo = getChild<LLComboBox>("start_location_combo");
	combo->setAllowTextEntry(TRUE, 128, FALSE);

	// The XML file loads the combo with the following labels:
	// 0 - "My Home"
	// 1 - "My Last Location"
	// 2 - "<Type region name>"

	BOOL login_last = gSavedSettings.getBOOL("LoginLastLocation");
	std::string sim_string = LLURLSimString::sInstance.mSimString;
	if (!sim_string.empty())
	{
		// Replace "<Type region name>" with this region name
		combo->remove(2);
		combo->add( sim_string );
		combo->setTextEntry(sim_string);
		combo->setCurrentByIndex( 2 );
	}
	else if (login_last)
	{
		combo->setCurrentByIndex( 1 );
	}
	else
	{
		combo->setCurrentByIndex( 0 );
	}


	LLComboBox* server_choice_combo = sInstance->getChild<LLComboBox>("server_combo");
	server_choice_combo->setCommitCallback(onSelectServer);
	//server_choice_combo->setFocusLostCallback(onServerComboLostFocus);

	childSetAction("connect_btn", onClickConnect, this);
	childSetAction("grid_btn", onClickGrid, this);

	setDefaultBtn("connect_btn");

	// childSetAction("quit_btn", onClickQuit, this);

	std::string imp_channel = gSavedSettings.getString("VersionChannelName");
	std::string imp_version = llformat("%d.%d.%d %s",
		ViewerVersion::getImpMajorVersion(),
		ViewerVersion::getImpMinorVersion(),
		ViewerVersion::getImpPatchVersion(),
		ViewerVersion::getImpTestVersion().c_str() );

	std::string ll_channel = ViewerVersion::getLLViewerName();
	std::string ll_version = llformat("%d.%d.%d (%d)",
		ViewerVersion::getLLMajorVersion(),
		ViewerVersion::getLLMinorVersion(),
		ViewerVersion::getLLPatchVersion(),
		ViewerVersion::getLLBuildVersion() );

	LLTextBox* channel_text = getChild<LLTextBox>("channel_text");
	channel_text->setTextArg("[CHANNEL]", imp_channel);
	channel_text->setTextArg("[VERSION]", imp_version);
	channel_text->setTextArg("[ALT_CHANNEL]", ll_channel);
	channel_text->setTextArg("[ALT_VERSION]", ll_version);
	channel_text->setClickedCallback(onClickVersion);
	channel_text->setCallbackUserData(this);
	
	LLTextBox* forgot_password_text = getChild<LLTextBox>("forgot_password_text");
	forgot_password_text->setClickedCallback(onClickForgotPassword);

	LLTextBox* create_new_account_text = getChild<LLTextBox>("create_new_account_text");
	create_new_account_text->setClickedCallback(onClickNewAccount);
#endif    
	
	// get the web browser control
	LLMediaCtrl* web_browser = getChild<LLMediaCtrl>("login_html");
	web_browser->addObserver(this);

	// Need to handle login secondlife:///app/ URLs
	web_browser->setTrusted( true );

	// don't make it a tab stop until SL-27594 is fixed
	web_browser->setTabStop(FALSE);
	// web_browser->navigateToLocalPage( "loading", "loading.html" );

	// make links open in external browser
	web_browser->setOpenInExternalBrowser( true );

	// force the size to be correct (XML doesn't seem to be sufficient to do this) (with some padding so the other login screen doesn't show through)
	LLRect htmlRect = getRect();
#if USE_VIEWER_AUTH
	htmlRect.setCenterAndSize( getRect().getCenterX() - 2, getRect().getCenterY(), getRect().getWidth() + 6, getRect().getHeight());
#else
	htmlRect.setCenterAndSize( getRect().getCenterX() - 2, getRect().getCenterY() + 40, getRect().getWidth() + 6, getRect().getHeight() - 78 );
#endif
	web_browser->setRect( htmlRect );
	web_browser->reshape( htmlRect.getWidth(), htmlRect.getHeight(), TRUE );
	reshape( getRect().getWidth(), getRect().getHeight(), 1 );

	// kick off a request to grab the url manually
	gResponsePtr = LLIamHereLogin::build( this );
	std::string login_page = gSavedSettings.getString("LoginPage");
	if (login_page.empty())
	{
		login_page = getString( "real_url" );
	}
	LLHTTPClient::head( login_page, gResponsePtr );

#if !USE_VIEWER_AUTH
	// Initialize visibility (and don't force visibility - use prefs)
	refreshLocation( false );
#endif

	LLFirstUse::useLoginScreen();
}

void LLPanelLogin::setSiteIsAlive( bool alive )
{
	LLMediaCtrl* web_browser = getChild<LLMediaCtrl>("login_html");
	// if the contents of the site was retrieved
	if ( alive )
	{
		if ( web_browser )
		{
			loadLoginPage();
			
			// mark as available
			mHtmlAvailable = TRUE;
		}
	}
	else
	// the site is not available (missing page, server down, other badness)
	{
#if !USE_VIEWER_AUTH
		if ( web_browser )
		{
			// hide browser control (revealing default one)
			web_browser->setVisible( FALSE );

			// mark as unavailable
			mHtmlAvailable = FALSE;
		}
#else

		if ( web_browser )
		{	
			web_browser->navigateToLocalPage( "loading-error" , "index.html" );

			// mark as available
			mHtmlAvailable = TRUE;
		}
#endif
	}
}

void LLPanelLogin::mungePassword(LLUICtrl* caller, void* user_data)
{
	LLPanelLogin* self = (LLPanelLogin*)user_data;
	LLLineEditor* editor = (LLLineEditor*)caller;
	std::string password = editor->getText();

	// Re-md5 if we've changed at all
	if (password != self->mIncomingPassword)
	{
		LLMD5 pass((unsigned char *)password.c_str());
		char munged_password[MD5HEX_STR_SIZE];
		pass.hex_digest(munged_password);
		self->mMungedPassword = munged_password;
	}
}

LLPanelLogin::~LLPanelLogin()
{
	LLPanelLogin::sInstance = NULL;

	// tell the responder we're not here anymore
	if ( gResponsePtr )
		gResponsePtr->setParent( 0 );

	//// We know we're done with the image, so be rid of it.
	//gImageList.deleteImage( mLogoImage );
	
	if ( gFocusMgr.getDefaultKeyboardFocus() == this )
	{
		gFocusMgr.setDefaultKeyboardFocus(NULL);
	}
}

// virtual
void LLPanelLogin::draw()
{
	glPushMatrix();
	{
		F32 image_aspect = 1.333333f;
		F32 view_aspect = (F32)getRect().getWidth() / (F32)getRect().getHeight();
		// stretch image to maintain aspect ratio
		if (image_aspect > view_aspect)
		{
			glTranslatef(-0.5f * (image_aspect / view_aspect - 1.f) * getRect().getWidth(), 0.f, 0.f);
			glScalef(image_aspect / view_aspect, 1.f, 1.f);
		}

		S32 width = getRect().getWidth();
		S32 height = getRect().getHeight();

		if ( mHtmlAvailable )
		{
#if !USE_VIEWER_AUTH
			// draw a background box in black
			gl_rect_2d( 0, height - 264, width, 264, LLColor4( 0.0f, 0.0f, 0.0f, 1.f ) );
			// draw the bottom part of the background image - just the blue background to the native client UI
			mLogoImage->draw(0, -264, width + 8, mLogoImage->getHeight());
#endif
		}
		else
		{
			// the HTML login page is not available so default to the original screen
			S32 offscreen_part = height / 3;
			mLogoImage->draw(0, -offscreen_part, width, height+offscreen_part);
		};
	}
	glPopMatrix();

	LLPanel::draw();
}

// virtual
BOOL LLPanelLogin::handleKeyHere(KEY key, MASK mask)
{
	if (( KEY_RETURN == key ) && (MASK_ALT == mask))
	{
		gViewerWindow->toggleFullscreenConfirm();
		return TRUE;
	}

	if (('P' == key) && (MASK_CONTROL == mask))
	{
		LLFloaterPreference::show(NULL);
		return TRUE;
	}

	if (('T' == key) && (MASK_CONTROL == mask))
	{
		new LLFloaterSimple("floater_test.xml");
		return TRUE;
	}
	
	if ( KEY_F1 == key )
	{
		llinfos << "Spawning HTML help window" << llendl;
		gViewerHtmlHelp.show();
		return TRUE;
	}

# if !LL_RELEASE_FOR_DOWNLOAD
	if ( KEY_F2 == key )
	{
		llinfos << "Spawning floater TOS window" << llendl;
		LLFloaterTOS* tos_dialog = LLFloaterTOS::show(LLFloaterTOS::TOS_TOS,"");
		tos_dialog->startModal();
		return TRUE;
	}
#endif

	if (KEY_RETURN == key && MASK_NONE == mask)
	{
		// let the panel handle UICtrl processing: calls onClickConnect()
		return LLPanel::handleKeyHere(key, mask);
	}

	return LLPanel::handleKeyHere(key, mask);
}

// virtual 
void LLPanelLogin::setFocus(BOOL b)
{
	if(b != hasFocus())
	{
		if(b)
		{
			LLPanelLogin::giveFocus();
		}
		else
		{
			LLPanel::setFocus(b);
		}
	}
}

// static
void LLPanelLogin::giveFocus()
{
#if USE_VIEWER_AUTH
	if (sInstance)
	{
		sInstance->setFocus(TRUE);
	}
#else
	if( sInstance )
	{
		// Grab focus and move cursor to first blank input field
		std::string first = sInstance->childGetText("first_name_edit");
		std::string pass = sInstance->childGetText("password_edit");

		BOOL have_first = !first.empty();
		BOOL have_pass = !pass.empty();

		LLLineEditor* edit = NULL;
		if (have_first && !have_pass)
		{
			// User saved his name but not his password.  Move
			// focus to password field.
			edit = sInstance->getChild<LLLineEditor>("password_edit");
		}
		else
		{
			// User doesn't have a name, so start there.
			edit = sInstance->getChild<LLLineEditor>("first_name_edit");
		}

		if (edit)
		{
			edit->setFocus(TRUE);
			edit->selectAll();
		}
	}
#endif
}


// static
void LLPanelLogin::show(const LLRect &rect,
						BOOL show_server,
						void (*callback)(S32 option, void* user_data),
						void* callback_data)
{
	new LLPanelLogin(rect, show_server, callback, callback_data);

	if( !gFocusMgr.getKeyboardFocus() )
	{
		// Grab focus and move cursor to first enabled control
		sInstance->setFocus(TRUE);
	}

	// Make sure that focus always goes here (and use the latest sInstance that was just created)
	gFocusMgr.setDefaultKeyboardFocus(sInstance);
	LLPanelLogin::addServer(LLViewerLogin::getInstance()->getGridLabel());
}

// static
void LLPanelLogin::setFields(const std::string& firstname,
			     const std::string& lastname,
			     const std::string& password)
{
	if (!sInstance)
	{
		llwarns << "Attempted fillFields with no login view shown" << llendl;
		return;
	}

	sInstance->childSetText("first_name_edit", firstname);
	sInstance->childSetText("last_name_edit", lastname);

	// Max "actual" password length is 16 characters.
	// Hex digests are always 32 characters.
	if (password.length() == 32)
	{
		// This is a MD5 hex digest of a password.
		// We don't actually use the password input field, 
		// fill it with MAX_PASSWORD characters so we get a 
		// nice row of asterixes.
		const std::string filler("123456789!123456");
		sInstance->childSetText("password_edit", filler);
		sInstance->mIncomingPassword = filler;
		sInstance->mMungedPassword = password;
	}
	else
	{
		// this is a normal text password
		sInstance->childSetText("password_edit", password);
		sInstance->mIncomingPassword = password;
		LLMD5 pass((unsigned char *)password.c_str());
		char munged_password[MD5HEX_STR_SIZE];
		pass.hex_digest(munged_password);
		sInstance->mMungedPassword = munged_password;
	}
}


// static
void LLPanelLogin::addServer(const std::string& server)
{
	if (!sInstance)
	{
		llwarns << "Attempted addServer with no login view shown" << llendl;
		return;
	}

	const std::string &defaultGrid = gHippoGridManager->getDefaultGridNick();

	LLComboBox *grids = sInstance->getChild<LLComboBox>("server_combo");
	S32 selectIndex = -1, i = 0;
	grids->removeall();
	if (defaultGrid != "") {
		grids->add(defaultGrid);
		selectIndex = i++;
	}
	HippoGridManager::GridIterator it, end = gHippoGridManager->endGrid();
	for (it = gHippoGridManager->beginGrid(); it != end; ++it) {
		const std::string &grid = it->second->getGridNick();
		if (grid != defaultGrid) {
			grids->add(grid);
			//if (grid == mCurGrid) selectIndex = i;
			i++;
		}
	}
	
	// when you first login select the default, otherwise last connected
	if (gDisconnected)
	{
		grids->setSimple(gHippoGridManager->getCurrentGrid()->getGridNick());
	}
	else
	{
		std::string last_grid = gSavedSettings.getString("CmdLineGridChoice");//imprudence TODO:errorcheck
		std::string cmd_line_login_uri = gSavedSettings.getLLSD("CmdLineLoginURI").asString();
		if (!last_grid.empty()&& cmd_line_login_uri.empty())//don't use --grid if --loginuri is also given
		{
			 //give user chance to change their mind, even with --grid set
			gSavedSettings.setString("CmdLineGridChoice","");
		}
		else if (!cmd_line_login_uri.empty())
		{
			last_grid = cmd_line_login_uri;
			 //also clear --grid no matter if it was given
			gSavedSettings.setString("CmdLineGridChoice","");
		}
		else if (last_grid.empty())
		{
			last_grid = gSavedSettings.getString("LastSelectedGrid");
		}
		if (last_grid.empty()) last_grid = defaultGrid;
		grids->setSimple(last_grid);
	}

	//LLComboBox* combo = sInstance->getChild<LLComboBox>("server_combo");
	//combo->add(server, LLSD(domain_name) );
	//combo->setCurrentByIndex(0);
}

// static
void LLPanelLogin::getFields(std::string *firstname,
			     std::string *lastname,
			     std::string *password)
{
	if (!sInstance)
	{
		llwarns << "Attempted getFields with no login view shown" << llendl;
		return;
	}

	*firstname = sInstance->childGetText("first_name_edit");
	LLStringUtil::trim(*firstname);

	*lastname = sInstance->childGetText("last_name_edit");
	LLStringUtil::trim(*lastname);

	*password = sInstance->mMungedPassword;
}

// static
BOOL LLPanelLogin::isGridComboDirty()
{
	BOOL user_picked = FALSE;
	if (!sInstance)
	{
		llwarns << "Attempted getServer with no login view shown" << llendl;
	}
	else
	{
		LLComboBox* combo = sInstance->getChild<LLComboBox>("server_combo");
		user_picked = combo->isDirty();
	}
	return user_picked;
}

// static
void LLPanelLogin::getLocation(std::string &location)
{
	if (!sInstance)
	{
		llwarns << "Attempted getLocation with no login view shown" << llendl;
		return;
	}
	
	LLComboBox* combo = sInstance->getChild<LLComboBox>("start_location_combo");
	location = combo->getValue().asString();
}

// static
void LLPanelLogin::refreshLocation( bool force_visible )
{
	if (!sInstance) return;

#if USE_VIEWER_AUTH
	loadLoginPage();
#else
	LLComboBox* combo = sInstance->getChild<LLComboBox>("start_location_combo");

	if (LLURLSimString::parse())
	{
		combo->setCurrentByIndex( 2 );
		combo->setTextEntry(LLURLSimString::sInstance.mSimString);
	}
	else
	{
		BOOL login_last = gSavedSettings.getBOOL("LoginLastLocation");
		combo->setCurrentByIndex( login_last ? 1 : 0 );
	}

	BOOL show_start = TRUE;

	if ( ! force_visible )
		show_start = gSavedSettings.getBOOL("ShowStartLocation");

// [RLVa:KB] - Alternate: Snowglobe-1.2.4 | Checked: 2009-07-08 (RLVa-1.0.0e)
	// TODO-RLVa: figure out some way to make this work with RLV_EXTENSION_STARTLOCATION
	#ifndef RLV_EXTENSION_STARTLOCATION
		if (rlv_handler_t::isEnabled())
		{
			show_start = FALSE;
		}
	#endif // RLV_EXTENSION_STARTLOCATION
// [/RLVa:KB]

	sInstance->childSetVisible("start_location_combo", show_start);
	sInstance->childSetVisible("start_location_text", show_start);

/*#if LL_RELEASE_FOR_DOWNLOAD
	BOOL show_server = gSavedSettings.getBOOL("ForceShowGrid");
	sInstance->childSetVisible("server_combo", show_server);
#else*/
	sInstance->childSetVisible("server_combo", TRUE);
//#endif

#endif
}

// static
void LLPanelLogin::close()
{
	if (sInstance)
	{
		gViewerWindow->getRootView()->removeChild( LLPanelLogin::sInstance );
		
		gFocusMgr.setDefaultKeyboardFocus(NULL);

		delete sInstance;
		sInstance = NULL;
	}
}

// static
void LLPanelLogin::setAlwaysRefresh(bool refresh)
{
	if (LLStartUp::getStartupState() >= STATE_LOGIN_CLEANUP) return;

	LLMediaCtrl* web_browser = sInstance->getChild<LLMediaCtrl>("login_html");

	if (web_browser)
	{
		web_browser->setAlwaysRefresh(refresh);
	}
}


// static
void LLPanelLogin::refreshLoginPage()
{
    if (!sInstance) return;

    sInstance->childSetVisible("create_new_account_text",
        !gHippoGridManager->getCurrentGrid()->getRegisterUrl().empty());
    sInstance->childSetVisible("forgot_password_text",
        !gHippoGridManager->getCurrentGrid()->getPasswordUrl().empty());

    // kick off a request to grab the url manually
	gResponsePtr = LLIamHereLogin::build(sInstance);
	std::string login_page = gHippoGridManager->getCurrentGrid()->getLoginPage();
	if (!login_page.empty()) 
	{
		LLHTTPClient::head(login_page, gResponsePtr);
	} 
	else 
	{
		sInstance->setSiteIsAlive(false);
	}
}


void LLPanelLogin::loadLoginPage()
{
	if (!sInstance) return;
	

	std::string login_page = gHippoGridManager->getCurrentGrid()->getLoginPage();
	if (login_page.empty()) 
	{
		sInstance->setSiteIsAlive(false);
		return;
	}

	std::ostringstream oStr;
	oStr << login_page;
	
	// Use the right delimeter depending on how LLURI parses the URL
	LLURI login_page_uri = LLURI(login_page);
	std::string first_query_delimiter = "&";
	if (login_page_uri.queryMap().size() == 0)
	{
		first_query_delimiter = "?";
	}

	// Language
	std::string language = LLUI::getLanguage();
	oStr << first_query_delimiter<<"lang=" << language;
	
	// First Login?
	if (gSavedSettings.getBOOL("FirstLoginThisInstall"))
	{
		oStr << "&firstlogin=TRUE";
	}

	// Channel and Version
	//Skip channel & version for now because using a non Linden channel breaks the SL
	//login page background, and because the info is used to upgrade the viewer, which
	//we're handling elsewhere now - Patrick Sapinski (2/5/2011)
	/*
	std::string version = llformat("%d.%d.%d %s",
		ViewerVersion::getImpMajorVersion(), ViewerVersion::getImpMinorVersion(),
		ViewerVersion::getImpPatchVersion(), ViewerVersion::getImpTestVersion().c_str() );

	char* curl_channel = curl_escape(gSavedSettings.getString("VersionChannelName").c_str(), 0);
	char* curl_version = curl_escape(version.c_str(), 0);

	oStr << "&channel=" << curl_channel;
	oStr << "&version=" << curl_version;

	curl_free(curl_channel);
	curl_free(curl_version);
	*/

	// Grid
	//char* curl_grid = curl_escape(LLViewerLogin::getInstance()->getGridLabel().c_str(), 0);
	//oStr << "&grid=" << curl_grid;
	//curl_free(curl_grid);

	gViewerWindow->setMenuBackgroundColor(false, !LLViewerLogin::getInstance()->isInProductionGrid());
	//LLViewerLogin::getInstance()->setMenuColor();
	gLoginMenuBarView->setBackgroundColor(gMenuBarView->getBackgroundColor());


#if USE_VIEWER_AUTH
	LLURLSimString::sInstance.parse();

	std::string location;
	std::string region;
	std::string password;
	
	if (LLURLSimString::parse())
	{
		std::ostringstream oRegionStr;
		location = "specify";
		oRegionStr << LLURLSimString::sInstance.mSimName << "/" << LLURLSimString::sInstance.mX << "/"
			 << LLURLSimString::sInstance.mY << "/"
			 << LLURLSimString::sInstance.mZ;
		region = oRegionStr.str();
	}
	else
	{
		if (gSavedSettings.getBOOL("LoginLastLocation"))
		{
			location = "last";
		}
		else
		{
			location = "home";
		}
	}

	std::string firstname, lastname;

	if(gSavedSettings.getLLSD("UserLoginInfo").size() == 3)
	{
		LLSD cmd_line_login = gSavedSettings.getLLSD("UserLoginInfo");
		firstname = cmd_line_login[0].asString();
		lastname = cmd_line_login[1].asString();
		password = cmd_line_login[2].asString();
	}

	if (firstname.empty())
	{
		firstname = gSavedSettings.getString("FirstName");
	}
	
	if (lastname.empty())
	{
		lastname = gSavedSettings.getString("LastName");
	}
	
	char* curl_region = curl_escape(region.c_str(), 0);

	oStr <<"firstname=" << firstname <<
		"&lastname=" << lastname << "&location=" << location <<	"&region=" << curl_region;
	
	curl_free(curl_region);

	if (!password.empty())
	{
		oStr << "&password=" << password;
	}
	else if (!(password = load_password_from_disk()).empty())
	{
		oStr << "&password=$1$" << password;
	}
	if (gAutoLogin)
	{
		oStr << "&auto_login=TRUE";
	}
	if (gSavedSettings.getBOOL("ShowStartLocation"))
	{
		oStr << "&show_start_location=TRUE";
	}	
	if (gSavedSettings.getBOOL("RememberPassword"))
	{
		oStr << "&remember_password=TRUE";
	}	
#ifndef	LL_RELEASE_FOR_DOWNLOAD
	oStr << "&show_grid=TRUE";
#else
	if (gSavedSettings.getBOOL("ForceShowGrid"))
		oStr << "&show_grid=TRUE";
#endif
#endif
	
	LLMediaCtrl* web_browser = sInstance->getChild<LLMediaCtrl>("login_html");
	
	// navigate to the "real" page 
	web_browser->navigateTo( oStr.str(), "text/html" );
}

void LLPanelLogin::handleMediaEvent(LLPluginClassMedia* /*self*/, EMediaEvent event)
{
	if(event == MEDIA_EVENT_NAVIGATE_COMPLETE)
	{
		LLMediaCtrl* web_browser = sInstance->getChild<LLMediaCtrl>("login_html");
		if (web_browser)
		{
			// *HACK HACK HACK HACK!
			/* Stuff a Tab key into the browser now so that the first field will
			** get the focus!  The embedded javascript on the page that properly
			** sets the initial focus in a real web browser is not working inside
			** the viewer, so this is an UGLY HACK WORKAROUND for now.
			*/
			// Commented out as it's not reliable
			//web_browser->handleKey(KEY_TAB, MASK_NONE, false);
		}
	}
}

//---------------------------------------------------------------------------
// Protected methods
//---------------------------------------------------------------------------

// static
void LLPanelLogin::onClickConnect(void *)
{
	if (sInstance && sInstance->mCallback)
	{
		// tell the responder we're not here anymore
		if ( gResponsePtr )
			gResponsePtr->setParent( 0 );

		// JC - Make sure the fields all get committed.
		sInstance->setFocus(FALSE);

		std::string first = sInstance->childGetText("first_name_edit");
		std::string last  = sInstance->childGetText("last_name_edit");
		if (!first.empty() && !last.empty())
		{
			// has both first and last name typed
			sInstance->mCallback(0, sInstance->mCallbackData);
		}
		else
		{
			LLNotifications::instance().add("MustHaveAccountToLogIn", LLSD(), LLSD(),
										LLPanelLogin::newAccountAlertCallback);
		}
	}
}

void LLPanelLogin::onClickGrid(void *)
{
	if (sInstance && sInstance->mCallback)
	{
		FloaterGridManager::getInstance()->open();
		FloaterGridManager::getInstance()->center();
	}
}

// static
bool LLPanelLogin::newAccountAlertCallback(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);
	if (0 == option)
	{
		onClickNewAccount(0);
	}
	else
	{
		sInstance->setFocus(TRUE);
	}
	return false;
}


// static
void LLPanelLogin::onClickNewAccount(void*)
{
	const std::string &url = gHippoGridManager->getConnectedGrid()->getRegisterUrl();
	if (!url.empty()) {
		llinfos << "Going to account creation URL." << llendl;
		LLWeb::loadURLExternal(url);
	} else {
		llinfos << "Account creation URL is empty." << llendl;
		sInstance->setFocus(TRUE);
	}
}


// *NOTE: This function is dead as of 2008 August.  I left it here in case
// we suddenly decide to put the Quit button back. JC
// static
void LLPanelLogin::onClickQuit(void*)
{
	if (sInstance && sInstance->mCallback)
	{
		// tell the responder we're not here anymore
		if ( gResponsePtr )
			gResponsePtr->setParent( 0 );

		sInstance->mCallback(1, sInstance->mCallbackData);
	}
}


// static
void LLPanelLogin::onClickVersion(void*)
{
	LLFloaterAbout::show(NULL);
}

//static
void LLPanelLogin::onClickForgotPassword(void*)
{
	if (sInstance )
	{
		const std::string &url = gHippoGridManager->getConnectedGrid()->getPasswordUrl();
		if (!url.empty()) {
			LLWeb::loadURLExternal(url);
		} else {
			llwarns << "Link for 'forgotton password' not set." << llendl;
		}
	}
}

// static
void LLPanelLogin::onPassKey(LLLineEditor* caller, void* user_data)
{
	if (gKeyboard->getKeyDown(KEY_CAPSLOCK) && sCapslockDidNotification == FALSE)
	{
		LLNotifications::instance().add("CapsKeyOn");
		sCapslockDidNotification = TRUE;
	}
}

// static
void LLPanelLogin::onSelectServer(LLUICtrl* ctrl, void*)
{
	// *NOTE: The paramters for this method are ignored. 
	// LLPanelLogin::onServerComboLostFocus(LLFocusableElement* fe, void*)
	// calls this method.
	updateGridCombo(LLStringUtil::null);
}

// static
void LLPanelLogin::updateGridCombo(std::string grid_nick)
{
	LLComboBox* combo = sInstance->getChild<LLComboBox>("server_combo");

	if (grid_nick.empty())
	{
		// The user twiddled with the grid choice ui.
		// apply the selection to the grid setting.
		//std::string grid_label;
		//S32 grid_index;
		
		grid_nick = combo->getValue().asString();
		
		HippoGridInfo *gridInfo = gHippoGridManager->getGrid(grid_nick);
		if (gridInfo) {
		 	//childSetText("gridnick", gridInfo->getGridNick());
		 	//platform->setCurrentByIndex(gridInfo->getPlatform());
		 	//childSetText("gridname", gridInfo->getGridName());
			LLPanelLogin::setFields( gridInfo->getFirstName(), gridInfo->getLastName(), gridInfo->getAvatarPassword());
		}
	}
	else
	{
		combo->setSimple(grid_nick);
	}
	
	gHippoGridManager->setCurrentGrid(grid_nick);

	llinfos << "current grid set to " << grid_nick << llendl;

	// grid changed so show new splash screen (possibly)
	loadLoginPage();

	// save grid choice to settings
	gSavedSettings.setString("LastSelectedGrid", grid_nick);
}
/*
void LLPanelLogin::onServerComboLostFocus(LLFocusableElement* fe, void*)
{
	LLComboBox* combo = sInstance->getChild<LLComboBox>("server_combo");
	if(fe == combo)
	{
		onSelectServer(combo, NULL);	
	}
}
*/
