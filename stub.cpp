/*
 * Fade To Black engine rewrite
 * Copyright (C) 2006-2012 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <getopt.h>
#include "file.h"
#include "game.h"
#include "mixer.h"
#include "sound.h"
#include "render.h"
#include "stub.h"

#ifdef __vita__
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/io/stat.h>
#endif

static const char *USAGE =
	"Fade2Black/OpenGL\n"
	"Usage: f2b [OPTIONS]...\n"
	"  --datapath=PATH             Path to data files (default '.')\n"
	"  --language=EN|FR|GR|SP|IT   Language files to use (default 'EN')\n"
	"  --playdemo                  Use inputs from .DEM files\n"
	"  --level=NUM                 Start at level NUM\n"
	"  --voice=EN|FR|GR            Voice files (default 'EN')\n"
	"  --subtitles                 Display cutscene subtitles\n"
	"  --savepath=PATH             Path to save files (default '.')\n"
	"  --fullscreen                Fullscreen display (stretched)\n"
	"  --fullscreen-ar             Fullscreen display (4:3 aspect ratio)\n"
	"  --soundfont=FILE            SoundFont (.sf2) file for music\n"
	"  --fog                       Enable fog rendering\n"
	"  --texturefilter=FILTER      Texture filter (default 'linear')\n"
	"  --texturescaler=NAME        Texture scaler (default 'scale2x')\n"
	"  --mouse                     Enable mouse controls\n"
;

static const struct {
	FileLanguage lang;
	const char *str;
	bool voice;
} _languages[] = {
	{ kFileLanguage_EN, "EN", true  },
	{ kFileLanguage_FR, "FR", true  },
	{ kFileLanguage_GR, "GR", true  },
	{ kFileLanguage_SP, "SP", false },
	{ kFileLanguage_IT, "IT", false }
};

static FileLanguage parseLanguage(const char *language) {
	if (language) {
		for (int i = 0; i < ARRAYSIZE(_languages); ++i) {
			if (strcasecmp(_languages[i].str, language) == 0) {
				return _languages[i].lang;
			}
		}
	}
	return kFileLanguage_EN;
}

static FileLanguage parseVoice(const char *voice, FileLanguage lang) {
	switch (lang) {
	case kFileLanguage_SP:
	case kFileLanguage_IT:
		if (voice) {
			for (int i = 0; i < ARRAYSIZE(_languages); ++i) {
				if (strcasecmp(_languages[i].str, voice) == 0) {
					if (_languages[i].voice) {
						return _languages[i].lang;
					}
					break;
				}
			}
		}
		// default to English
		return kFileLanguage_EN;
	default:
		// voice must match text for other languages
		return lang;
	}
}

static int getNextCutsceneNum(int num) {
	switch (num) {
	case 47: // logo ea
		return 39;
	case 39: // logo dsi
		return 13;
	case 13: // 'intro'
		return 37;
	case 37: // opening credits - 'title'
		return 53;
	case 53: // 'gendeb'
		return 29;
	// game completed
	case 48: // closing credits - 'mgm'
		return 44;
	case 44: // fade to black - 'fade1'
		return 13;
	}
	return -1;
}

static char *_dataPath;
static char *_savePath;

struct GameStub_F2B : GameStub {

	enum {
		kStateCutscene,
		kStateGame,
		kStateInventory,
		kStateCabinet,
		kStateMenu,
		kStateInstaller,
	};

	Render *_render;
	Game *_g;
	GameParams _params;
	FileLanguage  _fileLanguage, _fileVoice;
	int _displayMode;
	int _state, _nextState;
	int _slotState;
	bool _loadState, _saveState;
	char *_soundFont;
	RenderParams _renderParams;
	char *_textureFilter;
	char *_textureScaler;

	GameStub_F2B()
		: _render(0), _g(0),
		_fileLanguage(kFileLanguage_EN), _fileVoice(kFileLanguage_EN), _displayMode(kDisplayModeWindowed) {
		memset(&_params, 0, sizeof(_params));
		_soundFont = 0;
		memset(&_renderParams, 0, sizeof(_renderParams));
		_textureFilter = 0;
		_textureScaler = 0;
	}

	void setState(int state) {
		// release
		switch (_state) {
		case kStateCutscene:
			_render->resizeOverlay(0, 0);
			_render->setPalette(_g->_screenPalette, 0, 256);
			break;
		case kStateCabinet:
			_g->finiCabinet();
			break;
		case kStateMenu:
			_g->finiMenu();
			break;
		}
		// init
		switch (state) {
		case kStateCutscene:
			_g->_cut.load(_g->_cut._numToPlay);
			break;
		case kStateGame:
			_g->updatePalette();
			break;
		case kStateInventory:
			if (!_g->initInventory()) {
				return;
			}
			break;
		case kStateCabinet:
			_g->initCabinet();
			break;
		case kStateMenu:
			_g->initMenu();
			break;
		case kStateInstaller:
			_g->initInstaller();
			break;
		}
		_state = state;
	}

	virtual int setArgs(int argc, char *argv[]) {
		char *language = 0;
		char *voice = 0;
		_nextState = kStateCutscene;
		g_utilDebugMask = kDebug_INFO;
		while (1) {
			static struct option options[] = {
				{ "datapath",  required_argument, 0, 1 },
				{ "language",  required_argument, 0, 2 },
				{ "playdemo",  no_argument,       0, 3 },
				{ "level",     required_argument, 0, 4 },
				{ "voice",     required_argument, 0, 5 },
				{ "subtitles", no_argument,      0, 6 },
				{ "savepath",  required_argument, 0, 7 },
				{ "debug",     required_argument, 0, 8 },
				{ "fullscreen",    no_argument,   0,  9 },
				{ "fullscreen-ar", no_argument,   0, 10 },
				{ "alt-level", required_argument, 0, 11 },
				{ "soundfont", required_argument, 0, 12 },
				{ "fog",       no_argument,       0, 13 },
				{ "texturefilter", required_argument, 0, 14 },
				{ "texturescaler", required_argument, 0, 15 },
				{ "mouse",     no_argument,       0, 16 },
				{ "touch",     no_argument,       0, 17 },
#ifdef F2B_DEBUG
				{ "xpos_conrad",    required_argument, 0, 100 },
				{ "zpos_conrad",    required_argument, 0, 101 },
				{ "init_state",     required_argument, 0, 103 },
#endif
				{ 0, 0, 0, 0 }
			};
			int index;
			const int c = getopt_long(argc, argv, "", options, &index);
			if (c == -1) {
				break;
			}
			switch (c) {
			case 1:
				_dataPath = strdup(optarg);
				break;
			case 2:
				language = strdup(optarg);
				break;
			case 3:
				_params.playDemo = true;
				break;
			case 4:
				_params.levelNum = atoi(optarg);
				break;
			case 5:
				voice = strdup(optarg);
				break;
			case 6:
				_params.subtitles = true;
				break;
			case 7:
				_savePath = strdup(optarg);
				break;
			case 8:
				g_utilDebugMask |= atoi(optarg);
				break;
			case 9:
				_displayMode = kDisplayModeFullscreenStretch;
				break;
			case 10:
				_displayMode = kDisplayModeFullscreenAr;
				break;
			case 11: {
					static const char *levels[] = {
						"1", "2a", "2b", "2c", "3", "4a", "4b", "4c", "5a", "5b", "5c", "6a", "6b", 0
					};
					for (int i = 0; levels[i]; ++i) {
						if (strcasecmp(levels[i], optarg) == 0) {
							_params.levelNum = i;
							break;
						}
					}
				}
				break;
			case 12:
				_soundFont = strdup(optarg);
				_params.sf2 = _soundFont;
				break;
			case 13:
				_renderParams.fog = true;
				break;
			case 14:
				_textureFilter = strdup(optarg);
				_renderParams.textureFilter = _textureFilter;
				break;
			case 15:
				_textureScaler = strdup(optarg);
				_renderParams.textureScaler = _textureScaler;
				break;
			case 16:
				_params.mouseMode = true;
				break;
			case 17:
				_params.touchMode = true;
				break;
#ifdef F2B_DEBUG
			case 100:
				_params.xPosConrad = atoi(optarg);
				break;
			case 101:
				_params.zPosConrad = atoi(optarg);
				break;
			case 102: {
					static struct {
						const char *name;
						int state;
					} states[] = {
						{ "game", kStateGame },
						{ "installer", kStateInstaller },
						{ "menu", kStateMenu },
						{ 0, -1 },
					};
					for (int i = 0; states[i].name; ++i) {
						if (strcasecmp(states[i].name, optarg) == 0) {
							_nextState = states[i].state;
							break;
						}
					}
				}
				break;
#endif
			default:
				printf("%s\n", USAGE);
				return -1;
			}
		}
		_fileLanguage = parseLanguage(language);
		_fileVoice = parseVoice(voice, _fileLanguage);
		free(language);
		language = 0;
		free(voice);
		voice = 0;
		return 0;
	}
	virtual int getDisplayMode() {
		return _displayMode;
	}
	virtual bool hasCursor() {
		return _params.mouseMode || _params.touchMode;
	}
	virtual int init() {
#ifdef __vita__
		sceIoMkdir("ux0:data/f2bgl", 0777);
		sceIoMkdir("ux0:data/f2bgl/data", 0777);
		sceIoMkdir("ux0:data/f2bgl/saves", 0777);

		SceAppUtilInitParam init;
		SceAppUtilBootParam boot;
		memset(&init, 0, sizeof(SceAppUtilInitParam));
		memset(&boot, 0, sizeof(SceAppUtilBootParam));
		sceAppUtilInit(&init, &boot);

		int language = 0;
		sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &language);
		switch (language) {
		case SCE_SYSTEM_PARAM_LANG_FRENCH:
			_fileLanguage = _fileVoice = kFileLanguage_FR;
			break;
		case SCE_SYSTEM_PARAM_LANG_GERMAN:
			_fileLanguage = _fileVoice = kFileLanguage_GR;
			break;
		case SCE_SYSTEM_PARAM_LANG_SPANISH:
			_fileLanguage = kFileLanguage_SP;
			break;
		case SCE_SYSTEM_PARAM_LANG_ITALIAN:
			_fileLanguage = kFileLanguage_IT;
			break;
		default:
			break;
		}
		if (!fileInit(_fileLanguage, _fileVoice, "ux0:data/f2bgl/data", "ux0:data/f2bgl/saves")) {
			warning("Unable to find datafiles");
			return -2;
		}
#else
		if (!fileInit(_fileLanguage, _fileVoice, _dataPath ? _dataPath : ".", _savePath ? _savePath : ".")) {
			warning("Unable to find datafiles");
			return -2;
		}
#endif
		_render = new Render(&_renderParams);
		_g = new Game(_render, &_params);
		_g->init();
		_g->_cut._numToPlay = 47;
		_state = -1;
		setState(_nextState);
		_nextState = _state;
		_slotState = 0;
		_loadState = _saveState = false;
		return 0;
	}
	virtual void quit() {
		delete _g;
		delete _render;
		free(_dataPath);
		_dataPath = 0;
		free(_savePath);
		_savePath = 0;
		free(_soundFont);
		free(_textureFilter);
		free(_textureScaler);
		_dataPath = 0;
	}
	virtual StubMixProc getMixProc(int rate, int fmt, void (*lock)(int)) {
		StubMixProc mix;
		mix.proc = Mixer::mixCb;
		mix.data = &_g->_snd._mix;
		_g->_snd._mix.setFormat(rate, fmt);
		_g->_snd._mix._lock = lock;
		_g->_snd._musicKey = 0;
		_g->playMusic(1);
		return mix;
	}
	virtual void queueKeyInput(int keycode, int pressed) {
		switch (keycode) {
		case kKeyCodeLeft:
			if (pressed) _g->inp.dirMask |= kInputDirLeft;
			else _g->inp.dirMask &= ~kInputDirLeft;
			break;
		case kKeyCodeRight:
			if (pressed) _g->inp.dirMask |= kInputDirRight;
			else _g->inp.dirMask &= ~kInputDirRight;
			break;
		case kKeyCodeUp:
			if (pressed) _g->inp.dirMask |= kInputDirUp;
			else _g->inp.dirMask &= ~kInputDirUp;
			break;
		case kKeyCodeDown:
			if (pressed) _g->inp.dirMask |= kInputDirDown;
			else _g->inp.dirMask &= ~kInputDirDown;
			break;
		case kKeyCodeAlt:
			_g->inp.altKey = pressed;
			break;
		case kKeyCodeShift:
			_g->inp.shiftKey = pressed;
			break;
		case kKeyCodeCtrl:
			_g->inp.ctrlKey = pressed;
			break;
		case kKeyCodeSpace:
			_g->inp.spaceKey = pressed;
			break;
		case kKeyCodeTab:
			_g->inp.tabKey = pressed;
			break;
		case kKeyCodeEscape:
			_g->inp.escapeKey = pressed;
			break;
		case kKeyCodeI:
			_g->inp.inventoryKey = pressed;
			break;
		case kKeyCodeJ:
			_g->inp.jumpKey = pressed;
			break;
		case kKeyCodeU:
			_g->inp.useKey = pressed;
			break;
		case kKeyCodeReturn:
			_g->inp.enterKey = pressed;
			break;
		case kKeyCode1:
		case kKeyCode2:
		case kKeyCode3:
		case kKeyCode4:
		case kKeyCode5:
			_g->inp.numKeys[1 + keycode - kKeyCode1] = pressed;
			break;
		case kKeyCodePageUp:
			_g->inp.footStepKey = pressed;
			break;
		case kKeyCodePageDown:
			_g->inp.backStepKey = pressed;
			break;
		case kKeyCodeFarNear:
			_g->inp.farNear = pressed;
			break;
		case kKeyCodeCheatLifeCounter:
			_g->_cheats ^= kCheatLifeCounter;
			break;
		}
	}
	void queueTouchInput(int pointer, int x, int y, int down) {
		if (pointer >= 0 && pointer < kPlayerInputPointersCount) {
			_g->inp.pointers[pointer][1] = _g->inp.pointers[pointer][0];
			_g->inp.pointers[pointer][0].x = x;
			_g->inp.pointers[pointer][0].y = y;
			_g->inp.pointers[pointer][0].down = down != 0;
		}
	}
	virtual void doTick(unsigned int ticks, int *joystick) {
		if (_nextState != _state) {
			setState(_nextState);
		}
		_nextState = _state;
		switch (_state) {
		case kStateCutscene:
			if (!_g->_cut.update(ticks)) {
				_g->_cut.unload();
				int cutsceneNum = _g->_cut._numToPlay;
				if (!_g->_cut.isInterrupted()) {
					do {
						int num = _g->_cut.dequeue();
						if (num < 0) {
							num = getNextCutsceneNum(_g->_cut._numToPlay);
						}
						_g->_cut._numToPlay = num;
					} while (_g->_cut._numToPlay >= 0 && !_g->_cut.load(_g->_cut._numToPlay));
				} else {
					_g->_cut._numToPlay = -1;
				}
				if (_g->_cut._numToPlay < 0) {
					_nextState = kStateGame;
					if (_g->_level == kLevelGameOver || (g_isDemo && cutsceneNum == 43)) {
						// restart
						_g->_changeLevel = false;
						_g->_level = 0;
						_g->initLevel();
					}
				}
			}
			break;
		case kStateGame:
#ifdef __vita__
			joystick[kButton_CIRCLE] = kKeyCodeReturn;
			joystick[kButton_CROSS]  = kKeyCodeSpace;
			joystick[kButton_DOWN]   = kKeyCodeU;
			joystick[kButton_LEFT]   = 0;
			joystick[kButton_UP]     = kKeyCodeCtrl;
			joystick[kButton_RIGHT]  = 0;
#endif
			if (_g->_changeLevel) {
				_g->_changeLevel = false;
				_g->initLevel(true);
			} else if (_g->_endGame) {
				_g->_endGame = false;
				_g->initLevel();
			}
			_g->updateGameInput();
			_g->doTick();
			if (_g->inp.inventoryKey) {
				_g->inp.inventoryKey = false;
				_nextState = kStateInventory;
			} else if (_g->inp.escapeKey) {
				_g->inp.escapeKey = false;
				_nextState = kStateMenu;
			} else if (_g->_cut._numToPlay >= 0 && _g->_cut._numToPlayCounter == 0) {
				_nextState = kStateCutscene;
			} else if (_g->_cabinetItemCount != 0) {
				_nextState = kStateCabinet;
			}
			break;
		case kStateInventory:
			_g->updateInventoryInput();
			_g->doInventory();
			if (_g->inp.inventoryKey || _g->inp.escapeKey) {
				_g->inp.inventoryKey = false;
				_g->inp.escapeKey = false;
				_g->closeInventory();
				_nextState = kStateGame;
			}
			break;
		case kStateCabinet:
			_g->doCabinet();
			if (_g->_cabinetItemCount == 0) {
				_nextState = kStateGame;
			}
			break;
		case kStateMenu:
#ifdef __vita__
			joystick[kButton_CIRCLE] = 0;
			joystick[kButton_CROSS]  = kKeyCodeReturn;
			joystick[kButton_DOWN]   = kKeyCodeDown;
			joystick[kButton_LEFT]   = kKeyCodeLeft;
			joystick[kButton_UP]     = kKeyCodeUp;
			joystick[kButton_RIGHT]  = kKeyCodeRight;
#endif
			if (!_g->doMenu()) {
				_nextState = kStateGame;
			}
			if (_g->inp.escapeKey) {
				_g->inp.escapeKey = false;
				_nextState = kStateGame;
			}
			break;
		case kStateInstaller:
			_g->doInstaller();
			// _nextState = kStateGame
			break;
		}
		for (int pointer = 0; pointer < kPlayerInputPointersCount; ++pointer) {
			_g->inp.pointers[pointer][1].down = false;
		}
	}
	virtual void initGL(int w, int h, float *ar) {
		_render->resizeScreen(w, h, ar);
	}
	virtual void drawGL() {
		_render->drawOverlay();
		if (_loadState) {
			if (_state == kStateGame) {
				if (_g->loadGameState(_slotState)) {
					_g->setGameStateLoad(_slotState);
					debug(kDebug_INFO, "Loaded game state from slot %d", _slotState);
				}
			}
			_loadState = false;
		}
		if (_saveState) {
			if (_state == kStateGame) {
				if (_g->saveGameState(_slotState)) {
					_g->saveScreenshot(_slotState);
					_g->setGameStateSave(_slotState);
					debug(kDebug_INFO, "Saved game state to slot %d", _slotState);
				}
			}
			_saveState = false;
		}
	}
	virtual void saveState(int slot) {
		_slotState = slot;
		_saveState = true;
	}
	virtual void loadState(int slot) {
		_slotState = slot;
		_loadState = true;
	}
};

extern "C" {
	GameStub *GameStub_create() {
		return new GameStub_F2B;
	}
};
