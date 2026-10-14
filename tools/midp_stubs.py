#!/usr/bin/env python3
"""
Generate and compile the MIDP/CLDC signature stubs needed to build MIDlets with
a modern JDK.

WHY THIS EXISTS

javax.microedition.* does not exist in Java SE, so a modern javac cannot compile
a MIDlet at all without something to compile against.  In 2004 you used the
WTK's cldcapi.jar; that toolchain is gone, so this script generates the
signatures.

The stubs are used ONLY for compilation.  They are never packaged into the
generated JAR: the real implementations are the ones in src/midp/, written in
C.  A stub body therefore throws, so that a stub accidentally ending up on a
runtime classpath fails loudly instead of silently doing nothing.

Scope: the API surface a MIDlet actually calls, plus the constants.  Anything
missing shows up as a compile error naming the member, which is a clear enough
signal to add it here.

Usage:
    python3 tools/midp_stubs.py [--out DIR] [--quiet]
Prints the classpath to put on javac's -classpath.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# The signatures.  Kept terse: "name(args):ret" with a leading "static " or a
# "CONST type = value" form for constants.
# ---------------------------------------------------------------------------
CLASSES = {
    "javax/microedition/midlet/MIDletStateChangeException": {
        "extends": "Exception",
        "methods": ["public MIDletStateChangeException()",
                    "public MIDletStateChangeException(String)"],
    },
    "javax/microedition/midlet/MIDlet": {
        "extends": "Object", "abstract": True,
        "methods": [
            "protected MIDlet()",
            "protected abstract void startApp() throws javax.microedition.midlet.MIDletStateChangeException",
            "protected abstract void pauseApp()",
            "protected abstract void destroyApp(boolean) throws javax.microedition.midlet.MIDletStateChangeException",
            "public final void notifyDestroyed()",
            "public final void notifyPaused()",
            "public final void resumeRequest()",
            "public final String getAppProperty(String)",
            "public final boolean platformRequest(String)",
            "public final boolean checkPermission(String)",
        ],
    },
    "javax/microedition/lcdui/CommandListener": {
        "interface": True,
        "methods": ["void commandAction(Command, Displayable)"],
    },
    "javax/microedition/lcdui/ItemCommandListener": {
        "interface": True,
        "methods": ["void commandAction(Command, Item)"],
    },
    "javax/microedition/lcdui/ItemStateListener": {
        "interface": True,
        "methods": ["void itemStateChanged(Item)"],
    },
    "javax/microedition/lcdui/Command": {
        "constants": {"SCREEN": "1", "BACK": "2", "CANCEL": "3", "OK": "4",
                      "HELP": "5", "STOP": "6", "EXIT": "7", "ITEM": "8",
                      "PRIORITY_1": "1", "PRIORITY_2": "2"},
        "methods": [
            "public Command(String, int, int)",
            "public Command(String, int, int, int)",
            "public int getCommandType()",
            "public String getLabel()",
            "public String getLongLabel()",
            "public int getPriority()",
        ],
    },
    "javax/microedition/lcdui/Displayable": {
        "methods": [
            "public String getTitle()",
            "public void setTitle(String)",
            "public Ticker getTicker()",
            "public void setTicker(Ticker)",
            "public void addCommand(Command)",
            "public void removeCommand(Command)",
            "public void setCommandListener(CommandListener)",
            "public boolean isShown()",
            "public boolean isFullScreenMode()",
            "public int getWidth()",
            "public int getHeight()",
            # PROTECTED in MIDP, not public: a subclass that declares it
            # protected - which is what every MIDlet does, because that is the
            # signature the API documents - cannot narrow a public method, and
            # javac rejects the override.  Found building Game15MIDlet.
            "protected void sizeChanged(int, int)",
        ],
    },
    "javax/microedition/lcdui/Canvas": {
        "extends": "Displayable", "abstract": True,
        "constants": {"KEY_NUM0": "48", "KEY_NUM1": "49", "KEY_NUM2": "50",
                      "KEY_NUM3": "51", "KEY_NUM4": "52", "KEY_NUM5": "53",
                      "KEY_NUM6": "54", "KEY_NUM7": "55", "KEY_NUM8": "56",
                      "KEY_NUM9": "57", "KEY_STAR": "42", "KEY_POUND": "35",
                      "KEY_UP": "-1", "KEY_DOWN": "-2", "KEY_LEFT": "-3",
                      "KEY_RIGHT": "-4", "KEY_FIRE": "-5",
                      "KEY_SOFTKEY1": "-6", "KEY_SOFTKEY2": "-7",
                      "KEY_SOFTKEY3": "-8",
                      "UP": "1", "DOWN": "6", "LEFT": "2", "RIGHT": "5",
                      "FIRE": "8", "GAME_A": "9", "GAME_B": "10",
                      "GAME_C": "11", "GAME_D": "12"},
        "methods": [
            "protected Canvas()",
            "protected abstract void paint(Graphics)",
            "protected void keyPressed(int)",
            "protected void keyReleased(int)",
            "protected void keyRepeated(int)",
            "protected void pointerPressed(int, int)",
            "protected void pointerReleased(int, int)",
            "protected void pointerDragged(int, int)",
            "public final void repaint()",
            "public final void repaint(int, int, int, int)",
            "public final void serviceRepaints()",
            "public void setFullScreenMode(boolean)",
            "public boolean isFullScreenMode()",
            "public boolean hasPointerEvents()",
            "public boolean hasPointerMotionEvents()",
            "public static int getGameAction(int)",
            "public static int getKeyCode(int)",
            "public static String getKeyName(int)",
            "public final boolean isDoubleBuffered()",
            "public int getWidth()",
            "public int getHeight()",
        ],
    },
    "javax/microedition/lcdui/Graphics": {
        "constants": {"HCENTER": "1", "VCENTER": "2", "LEFT": "4", "RIGHT": "8",
                      "TOP": "16", "BOTTOM": "32", "BASELINE": "64",
                      "SOLID": "0", "DOTTED": "1"},
        "methods": [
            "public void translate(int, int)",
            "public int getTranslateX()", "public int getTranslateY()",
            "public int getColor()",
            "public int getRedComponent()", "public int getGreenComponent()",
            "public int getBlueComponent()", "public int getAlphaComponent()",
            "public int getGrayScale()",
            "public void setColor(int)",
            "public void setColor(int, int, int)",
            "public void setGrayScale(int)",
            "public void setAlpha(int)",
            "public int getBlendMode()", "public void setBlendMode(int)",
            "public int getStrokeStyle()", "public void setStrokeStyle(int)",
            "public Font getFont()", "public void setFont(Font)",
            "public void setClip(int, int, int, int)",
            "public void clipRect(int, int, int, int)",
            "public int getClipX()", "public int getClipY()",
            "public int getClipWidth()", "public int getClipHeight()",
            "public void setXORMode(boolean)",
            "public void drawLine(int, int, int, int)",
            "public void drawRect(int, int, int, int)",
            "public void fillRect(int, int, int, int)",
            "public void drawRoundRect(int, int, int, int, int, int)",
            "public void fillRoundRect(int, int, int, int, int, int)",
            "public void drawArc(int, int, int, int, int, int)",
            "public void fillArc(int, int, int, int, int, int)",
            "public void drawTriangle(int, int, int, int, int, int)",
            "public void fillTriangle(int, int, int, int, int, int)",
            "public void drawString(String, int, int, int)",
            "public void drawString(String, int, int)",
            "public void drawSubstring(String, int, int, int, int, int)",
            "public void drawChar(char, int, int, int)",
            "public void drawChars(char[], int, int, int, int, int)",
            "public void drawImage(Image, int, int, int)",
            "public void drawRegion(Image, int, int, int, int, int, int, int, int)",
            "public void drawRegion(Image, int, int, int, int, int, int, int, int, int)",
            "public void drawRGB(int[], int, int, int, int, int, int, boolean)",
            "public void copyArea(int, int, int, int, int, int, int)",
        ],
    },
    "javax/microedition/lcdui/Image": {
        "methods": [
            "public static Image createImage(int, int)",
            "public static Image createImage(Image)",
            "public static Image createImage(int, int, int, int, int, int)",
            "public static Image createImage(byte[], int, int)",
            "public static Image createImage(byte[], int, int, int)",
            "public static Image createImage(String)",
            "public static Image createImage(java.io.InputStream)",
            "public static Image createRGBImage(int[], int, int, boolean)",
            "public Graphics getGraphics()",
            "public int getWidth()", "public int getHeight()",
            "public boolean isMutable()",
            "public void getRGB(int[], int, int, int, int, int, int)",
        ],
    },
    "javax/microedition/lcdui/Font": {
        "constants": {"FACE_SYSTEM": "0", "FACE_MONOSPACE": "32",
                      "FACE_PROPORTIONAL": "64",
                      "STYLE_PLAIN": "0", "STYLE_BOLD": "1",
                      "STYLE_ITALIC": "2", "STYLE_UNDERLINED": "4",
                      "SIZE_SMALL": "8", "SIZE_MEDIUM": "0", "SIZE_LARGE": "16"},
        "methods": [
            "public static Font getFont(int)",
            "public static Font getDefaultFont()",
            "public static Font getFont(int, int, int)",
            "public int getFace()", "public int getStyle()", "public int getSize()",
            "public boolean isPlain()", "public boolean isBold()",
            "public boolean isItalic()", "public boolean isUnderlined()",
            "public int getHeight()", "public int getBaselinePosition()",
            "public int charWidth(char)", "public int charsWidth(char[], int, int)",
            "public int stringWidth(String)", "public int substringWidth(String, int, int)",
            "public int getAscent()", "public int getDescent()",
        ],
    },
    "javax/microedition/lcdui/Ticker": {
        "methods": ["public Ticker(String)", "public String getString()",
                    "public void setString(String)"],
    },
    "javax/microedition/lcdui/AlertType": {
        "methods": [
            "public boolean playSound(Display)",
            "public String getTypeName(AlertType)",
        ],
    },
    "javax/microedition/lcdui/Alert": {
        "extends": "Screen",
        # DISMISS_COMMAND is a Command in the runtime, not an Object, and the
        # descriptor is part of the field's identity.
        "constants": {"FOREVER": "-2",
                      "DISMISS_COMMAND":
                          "null:Ljavax/microedition/lcdui/Command;"},
        "methods": [
            "public Alert(String)",
            "public Alert(String, String, Image, AlertType)",
            "public void setType(AlertType)", "public AlertType getType()",
            "public void setString(String)", "public String getString()",
            "public void setImage(Image)", "public Image getImage()",
            "public void setTimeout(int)", "public int getTimeout()",
            "public void setIndicator(Gauge)",
            "public static void setDefaultTimeout(int)",
            "public static int getDefaultTimeout()",
        ],
    },
    "javax/microedition/lcdui/Display": {
        "methods": [
            "public static Display getDisplay(MIDlet)",
            "public void setCurrent(Displayable)",
            "public void setCurrent(Alert, Displayable)",
            "public void setCurrentItem(Item)",
            "public Displayable getCurrent()",
            "public void callSerially(Runnable)",
            "public boolean isColor()",
            "public int numColors()", "public int numAlphaLevels()",
            "public void flashBacklight(int)",
            "public boolean vibrate(int)",
            "public int getBestImageWidth(int)", "public int getBestImageHeight(int)",
        ],
    },
    "javax/microedition/lcdui/Item": {
        "constants": {"LAYOUT_DEFAULT": "0", "LAYOUT_LEFT": "1",
                      "LAYOUT_RIGHT": "2", "LAYOUT_CENTER": "3",
                      "LAYOUT_TOP": "16", "LAYOUT_BOTTOM": "32",
                      "LAYOUT_VCENTER": "48", "LAYOUT_NEWLINE_BEFORE": "256",
                      "LAYOUT_NEWLINE_AFTER": "512",
                      "LAYOUT_SHRINK": "1024", "LAYOUT_VSHRINK": "2048",
                      "LAYOUT_EXPAND": "4096", "LAYOUT_VEXPAND": "8192"},
        "methods": [
            "public String getLabel()", "public void setLabel(String)",
            "public int getLayout()", "public void setLayout(int)",
            "public void addCommand(Command)", "public void removeCommand(Command)",
            "public void setItemCommandListener(ItemCommandListener)",
            "public void setPreferredSize(int, int)",
            "public int getMinimumWidth()", "public int getMinimumHeight()",
            "public int getPreferredWidth()", "public int getPreferredHeight()",
            "public void notifyStateChanged()",
            "public void setDefaultCommand(Command)",
        ],
    },
    "javax/microedition/lcdui/StringItem": {
        "extends": "Item",
        "methods": ["public StringItem(String, String)",
                    "public String getText()", "public void setText(String)",
                    "public Font getFont()", "public void setFont(Font)"],
    },
    "javax/microedition/lcdui/ImageItem": {
        "extends": "Item",
        "constants": {"LAYOUT_DEFAULT": "0", "LAYOUT_LEFT": "1",
                      "LAYOUT_RIGHT": "2", "LAYOUT_CENTER": "3",
                      "LAYOUT_NEWLINE_BEFORE": "256",
                      "LAYOUT_NEWLINE_AFTER": "512"},
        "methods": ["public ImageItem(String, Image, int, String)",
                    "public Image getImage()", "public void setImage(Image)",
                    "public String getAltText()", "public void setAltText(String)"],
    },
    "javax/microedition/lcdui/Gauge": {
        "extends": "Item",
        "constants": {"INDEFINITE": "-1", "CONTINUOUS_IDLE": "0",
                      "INCREMENTAL_IDLE": "1", "CONTINUOUS_RUNNING": "2",
                      "INCREMENTAL_UPDATING": "3"},
        "methods": ["public Gauge(String, boolean, int, int)",
                    "public int getValue()", "public void setValue(int)",
                    "public int getMaxValue()", "public void setMaxValue(int)",
                    "public boolean isInteractive()"],
    },
    "javax/microedition/lcdui/Choice": {
        "interface": True,
        "constants": {"EXCLUSIVE": "1", "MULTIPLE": "2", "IMPLICIT": "3",
                      "TEXT_WRAP_DEFAULT": "0", "TEXT_WRAP_ON": "1",
                      "TEXT_WRAP_OFF": "2"},
        "methods": [
            "int size()", "String getString(int)", "Image getImage(int)",
            "void append(String, Image)", "void insert(int, String, Image)",
            "void delete(int)", "void deleteAll()",
            "void set(int, String, Image)", "boolean isSelected(int)",
            "int getSelectedIndex()", "int getSelectedFlags(boolean[])",
            "void setSelectedIndex(int, boolean)", "void setSelectedFlags(boolean[])",
            "void setFitPolicy(int)", "int getFitPolicy()", "void setFont(int, Font)",
            "Font getFont(int)",
        ],
    },
    "javax/microedition/lcdui/ChoiceGroup": {
        "extends": "Item",
        "methods": [
            "public ChoiceGroup(String, int)",
            "public ChoiceGroup(String, int, String[], Image[])",
            "public int size()", "public String getString(int)",
            "public Image getImage(int)", "public void append(String, Image)",
            "public void insert(int, String, Image)", "public void delete(int)",
            "public void deleteAll()", "public void set(int, String, Image)",
            "public boolean isSelected(int)", "public int getSelectedIndex()",
            "public int getSelectedFlags(boolean[])",
            "public void setSelectedIndex(int, boolean)",
            "public void setSelectedFlags(boolean[])",
            "public void setFitPolicy(int)", "public int getFitPolicy()",
            "public void setFont(int, Font)", "public Font getFont(int)",
        ],
    },
    "javax/microedition/lcdui/TextField": {
        "extends": "Item",
        "constants": {"ANY": "0", "EMAILADDR": "1", "NUMERIC": "2",
                      "PHONENUMBER": "3", "URL": "4", "DECIMAL": "5",
                      "PASSWORD": "65536", "UNEDITABLE": "131072",
                      "SENSITIVE": "262144", "NON_PREDICTIVE": "524288",
                      "INITIAL_CAPS_WORD": "1048576",
                      "INITIAL_CAPS_SENTENCE": "2097152"},
        "methods": [
            "public TextField(String, String, int, int)",
            "public String getString()", "public void setString(String)",
            "public int getChars(char[])", "public void setChars(char[], int, int)",
            "public int size()", "public int getMaxSize()", "public void setMaxSize(int)",
            "public int getConstraints()", "public void setConstraints(int)",
            "public void insert(String, int)", "public void insert(char[], int, int, int)",
            "public void delete(int, int)",
            "public void setInitialInputMode(String)",
        ],
    },
    "javax/microedition/lcdui/DateField": {
        "extends": "Item",
        "constants": {"DATE": "1", "TIME": "2", "DATE_TIME": "3"},
        "methods": ["public DateField(String, int)",
                    "public DateField(String, int, java.util.TimeZone)",
                    "public java.util.Date getDate()",
                    "public void setDate(java.util.Date)",
                    "public int getInputMode()", "public void setInputMode(int)",
                    "public java.util.TimeZone getTimeZone()",
                    "public void setTimeZone(java.util.TimeZone)"],
    },
    "javax/microedition/lcdui/Spacer": {
        "extends": "Item",
        "methods": ["public Spacer(int, int)"],
    },
    "javax/microedition/lcdui/Screen": {
        "extends": "Displayable",
        "methods": ["public Ticker getTicker()", "public void setTicker(Ticker)",
                    "public String getTitle()", "public void setTitle(String)"],
    },
    "javax/microedition/lcdui/Form": {
        "extends": "Screen",
        "methods": [
            "public Form(String)", "public Form(String, Item[])",
            "public int append(Item)", "public int append(Image)",
            "public int append(String)",
            "public void insert(int, Item)", "public void delete(int)",
            "public void deleteAll()", "public void set(int, Item)",
            "public Item get(int)", "public int size()",
            "public void setItemStateListener(ItemStateListener)",
        ],
    },
    "javax/microedition/lcdui/List": {
        "extends": "Screen",
        # The three selection-mode constants.  Real MIDP has them, and a game
        # that writes List.IMPLICIT does not compile without them - the same
        # class of gap as the dropped throws clause and the dropped interface
        # parent: the stub has to be faithful or it rejects working code.
        # The REAL MIDP values.  These were 1/2/3 in the wrong order, and it
        # mattered: a `public static final int` is INLINED by javac at every use
        # site, so a MIDlet compiled against the stub passed the stub's value
        # while the runtime compared against its own.  List.IMPLICIT read as 1
        # (EXCLUSIVE) in the bytecode and 3 in the runtime, so every MIDlet
        # asking for an implicit list got an exclusive one.
        #
        # Any constant in this file is a promise about what the runtime will
        # contain; the two must agree or the stub is worse than no stub.
        "constants": {"EXCLUSIVE": "1", "MULTIPLE": "2", "IMPLICIT": "3"},
        "methods": [
            "public List(String, int)",
            "public List(String, int, String[], Image[])",
            "public int append(String, Image)", "public void insert(int, String, Image)",
            "public void delete(int)", "public void deleteAll()",
            "public void set(int, String, Image)", "public String getString(int)",
            "public Image getImage(int)", "public int size()",
            "public boolean isSelected(int)", "public int getSelectedIndex()",
            "public int getSelectedFlags(boolean[])",
            "public void setSelectedIndex(int, boolean)",
            "public void setSelectedFlags(boolean[])",
            "public void setFitPolicy(int)", "public int getFitPolicy()",
            "public void setFont(int, Font)", "public Font getFont(int)",
            "public void setSelectCommand(Command)",
        ],
    },
    "javax/microedition/lcdui/TextBox": {
        "extends": "Screen",
        "methods": [
            "public TextBox(String, String, int, int)",
            "public String getString()", "public void setString(String)",
            "public int getChars(char[])", "public void setChars(char[], int, int)",
            "public int size()", "public int getMaxSize()", "public void setMaxSize(int)",
            "public int getConstraints()", "public void setConstraints(int)",
            "public void insert(String, int)", "public void delete(int, int)",
            "public void setInitialInputMode(String)",
        ],
    },
    "javax/microedition/lcdui/game/Layer": {
        "abstract": True,
        "methods": [
            "protected Layer()",
            "public int getX()", "public int getY()",
            "public int getWidth()", "public int getHeight()",
            "public void setPosition(int, int)", "public void move(int, int)",
            "public final boolean isVisible()", "public final void setVisible(boolean)",
            "public abstract void paint(Graphics)",
        ],
    },
    "javax/microedition/lcdui/game/Sprite": {
        "extends": "Layer",
        "methods": [
            "public Sprite(Image)",
            "public Sprite(Image, int, int)",
            "public Sprite(Sprite)",
            "public void setImage(Image, int, int)",
            "public void setImage(Image)",
            "public Image getImage()",
            "public void setFrame(int)", "public int getFrame()",
            "public int getRawFrameCount()",
            "public void setFrameSequence(int[])", "public int getFrameSequenceLength()",
            "public int nextFrame()", "public int prevFrame()",
            "public int getFrameSequenceIndex()",
            "public void setTransform(int)", "public int getTransform()",
            "public void defineReferencePixel(int, int)",
            "public void setRefPixelPosition(int, int)",
            "public int getRefPixelX()", "public int getRefPixelY()",
            "public void setPosition(int, int)", "public void move(int, int)",
            "public final void paint(Graphics)",
            # The 2-argument forms are what MIDP actually documents and what
            # games call; only the Image form takes x, y and pixelLevel.
            # Found building FlappyPlaneJM, which calls collidesWith(s, true).
            "public final boolean collidesWith(Image, int, int, boolean)",
            "public final boolean collidesWith(Sprite, boolean)",
            "public final boolean collidesWith(TiledLayer, boolean)",
            "public final boolean collidesWith(Layer, boolean)",
            "public final boolean collidesWith(Sprite, boolean, boolean)",
            "public final boolean collidesWith(TiledLayer, boolean, boolean)",
            "public final boolean collidesWith(Layer, boolean, boolean)",
            "public void defineCollisionRectangle(int, int, int, int)",
            "public static final int TRANS_NONE",
            "public static final int TRANS_ROT90",
            "public static final int TRANS_ROT180",
            "public static final int TRANS_ROT270",
            "public static final int TRANS_MIRROR",
            "public static final int TRANS_MIRROR_ROT90",
            "public static final int TRANS_MIRROR_ROT180",
            "public static final int TRANS_MIRROR_ROT270",
        ],
    },
    "javax/microedition/lcdui/game/TiledLayer": {
        "extends": "Layer",
        "methods": [
            "public TiledLayer(int, int, Image, int, int)",
            "public void setStaticTileSet(Image, int, int)",
            "public void setCell(int, int, int)", "public int getCell(int, int)",
            "public void setAnimatedTile(int, int)", "public int getAnimatedTile(int)",
            "public int createAnimatedTile(int)",
            "public void fillCells(int, int, int, int, int)",
            "public void fillCells(int, int, int, int)",
            "public final int getCellWidth()", "public final int getCellHeight()",
            "public final int getColumns()", "public final int getRows()",
            "public final void paint(Graphics)",
        ],
    },
    "javax/microedition/lcdui/game/LayerManager": {
        "methods": [
            "public LayerManager()",
            "public void append(Layer)", "public void insert(Layer, int)",
            "public void remove(Layer)", "public Layer getLayerAt(int)",
            "public int getSize()",
            "public void setViewWindow(int, int, int, int)",
            "public int getViewX()", "public int getViewY()",
            "public void paint(Graphics, int, int)",
        ],
    },
    "javax/microedition/lcdui/game/GameCanvas": {
        "extends": "Canvas",
        "methods": [
            "protected GameCanvas(boolean)",
            "public Graphics getGraphics()",
            "protected void flushGraphics()",
            "protected void flushGraphics(int, int, int, int)",
            "public void setFullScreenMode(boolean)",
            "public boolean isFullScreenMode()",
            "public int getKeyStates()",
            "protected void paint(Graphics)",
            "public static final int UP_PRESSED",
            "public static final int DOWN_PRESSED",
            "public static final int LEFT_PRESSED",
            "public static final int RIGHT_PRESSED",
            "public static final int FIRE_PRESSED",
            "public static final int GAME_A_PRESSED",
            "public static final int GAME_B_PRESSED",
            "public static final int GAME_C_PRESSED",
            "public static final int GAME_D_PRESSED",
        ],
    },
    "javax/microedition/rms/RecordStore": {
        # The throws clauses are not decoration: a game that catches
        # RecordStoreFullException - which is what every game that saves a high
        # score does - does not compile against a stub that omits it, because
        # javac rejects a catch for an exception that cannot be thrown.  Found
        # by building a real GPL game (tools/make_bubblet_jar.sh).
        "methods": [
            "public static RecordStore openRecordStore(String, boolean) throws javax.microedition.rms.RecordStoreException, javax.microedition.rms.RecordStoreFullException, javax.microedition.rms.RecordStoreNotFoundException",
            "public static RecordStore openRecordStore(String, boolean, boolean) throws javax.microedition.rms.RecordStoreException, javax.microedition.rms.RecordStoreFullException, javax.microedition.rms.RecordStoreNotFoundException",
            "public static void deleteRecordStore(String) throws javax.microedition.rms.RecordStoreException, javax.microedition.rms.RecordStoreNotFoundException",
            "public static String[] listRecordStores() throws javax.microedition.rms.RecordStoreException",
            "public String getName() throws javax.microedition.rms.RecordStoreNotOpenException",
            "public int getNumRecords() throws javax.microedition.rms.RecordStoreNotOpenException",
            "public int getSize() throws javax.microedition.rms.RecordStoreNotOpenException",
            "public int getSizeAvailable() throws javax.microedition.rms.RecordStoreNotOpenException",
            "public int getRecordSize(int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "public int getNextRecordID() throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "public int getLastModified() throws javax.microedition.rms.RecordStoreNotOpenException",
            "public int getVersion() throws javax.microedition.rms.RecordStoreNotOpenException",
            "public int addRecord(byte[], int, int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreFullException, javax.microedition.rms.RecordStoreException",
            "public int addRecord(byte[], int, int, int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreFullException, javax.microedition.rms.RecordStoreException",
            "public byte[] getRecord(int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "public int getRecord(int, byte[], int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "public void setRecord(int, byte[], int, int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreFullException, javax.microedition.rms.RecordStoreException",
            "public void deleteRecord(int) throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "public void closeRecordStore() throws javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "public void addRecordListener(javax.microedition.rms.RecordListener) throws javax.microedition.rms.RecordStoreNotOpenException",
            "public void removeRecordListener(javax.microedition.rms.RecordListener) throws javax.microedition.rms.RecordStoreNotOpenException",
            "public javax.microedition.rms.RecordEnumeration enumerateRecords(javax.microedition.rms.RecordFilter, javax.microedition.rms.RecordComparator, boolean) throws javax.microedition.rms.RecordStoreNotOpenException"
        ],
    },
    # MIDP RMS enumeration.  A game that keeps a high-score table sorts and
    # filters it through these three, and 五子棋 died on the missing
    # enumerateRecords() the first time it tried to read its save file.
    "javax/microedition/rms/RecordFilter": {
        "interface": True,
        "methods": ["boolean matches(byte[])"],
    },
    "javax/microedition/rms/RecordComparator": {
        "interface": True,
        "constants": {"EQUIVALENT": "0", "PRECEDES": "-1", "FOLLOWS": "1"},
        "methods": ["int compare(byte[], byte[])"],
    },
    "javax/microedition/rms/RecordEnumeration": {
        "interface": True,
        "methods": [
            "int numRecords()",
            "byte[] nextRecord() throws javax.microedition.rms.InvalidRecordIDException, javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "int nextRecordId() throws javax.microedition.rms.InvalidRecordIDException, javax.microedition.rms.RecordStoreNotOpenException",
            "byte[] previousRecord() throws javax.microedition.rms.InvalidRecordIDException, javax.microedition.rms.RecordStoreNotOpenException, javax.microedition.rms.RecordStoreException",
            "int previousRecordId() throws javax.microedition.rms.InvalidRecordIDException, javax.microedition.rms.RecordStoreNotOpenException",
            "boolean hasNextElement()",
            "boolean hasPreviousElement()",
            "void reset()",
            "void rebuild() throws javax.microedition.rms.RecordStoreNotOpenException",
            "void destroy()",
            "boolean isKeptUpdated()",
            "void keepUpdated(boolean)",
        ],
    },
    "javax/microedition/rms/RecordListener": {
        "interface": True,
        "methods": ["void recordAdded(RecordStore, int)",
                    "void recordChanged(RecordStore, int)",
                    "void recordDeleted(RecordStore, int)"],
    },
    "javax/microedition/rms/RecordStoreException": {
        "extends": "Exception",
        "methods": ["public RecordStoreException()",
                    "public RecordStoreException(String)"],
    },
    "javax/microedition/rms/RecordStoreNotFoundException": {
        "extends": "RecordStoreException",
        "methods": ["public RecordStoreNotFoundException()",
                    "public RecordStoreNotFoundException(String)"],
    },
    "javax/microedition/rms/RecordStoreNotOpenException": {
        "extends": "RecordStoreException",
        "methods": ["public RecordStoreNotOpenException()",
                    "public RecordStoreNotOpenException(String)"],
    },
    "javax/microedition/rms/RecordStoreFullException": {
        "extends": "RecordStoreException",
        "methods": ["public RecordStoreFullException()",
                    "public RecordStoreFullException(String)"],
    },
    "javax/microedition/rms/InvalidRecordIDException": {
        "extends": "RecordStoreException",
        "methods": ["public InvalidRecordIDException()",
                    "public InvalidRecordIDException(String)"],
    },
    # java.io.Reader/Writer/InputStreamReader/OutputStreamWriter are NOT stubbed
    # here, and cannot be: javac refuses to compile classes into the java.io
    # package because it belongs to the java.base module ("package exists in
    # another module").  They come from the JDK, which is correct - all four are
    # in CLDC 1.1 - and this is exactly why tools/check_cldc_api.py audits the
    # compiled OUTPUT rather than trusting the classpath.
    "javax/microedition/io/Connector": {
        "constants": {"READ": "1", "WRITE": "2", "READ_WRITE": "3"},
        "methods": [
            "public static javax.microedition.io.Connection open(String)",
            "public static javax.microedition.io.Connection open(String, int)",
            "public static java.io.DataInputStream openDataInputStream(String)",
            "public static java.io.DataOutputStream openDataOutputStream(String)",
            "public static java.io.InputStream openInputStream(String)",
            "public static java.io.OutputStream openOutputStream(String)",
        ],
    },
    "javax/microedition/io/Connection": {
        "interface": True, "methods": ["void close()"],
    },
    "javax/microedition/io/InputConnection": {
        "interface": True, "methods": ["java.io.DataInputStream openDataInputStream()",
                                       "java.io.InputStream openInputStream()"],
    },
    "javax/microedition/io/OutputConnection": {
        "interface": True, "methods": ["java.io.DataOutputStream openDataOutputStream()",
                                       "java.io.OutputStream openOutputStream()"],
    },
    # MIDP 2.0 makes HttpConnection mandatory.  The runtime implements it on
    # top of the port's HTTP hook (src/midp/nj_lib_http.c); the fixtures that
    # exercise it are built against these stubs.
    # StreamConnection really extends InputConnection and OutputConnection;
    # the stub generator gives an interface one parent, so the four stream
    # methods are declared here, exactly as the runtime declares the union on
    # the classes it hands out.
    "javax/microedition/io/StreamConnection": {
        "interface": True, "extends": "javax.microedition.io.Connection",
        "methods": [
            "java.io.InputStream openInputStream()",
            "java.io.DataInputStream openDataInputStream()",
            "java.io.OutputStream openOutputStream()",
            "java.io.DataOutputStream openDataOutputStream()",
        ],
    },
    "javax/microedition/io/ContentConnection": {
        "interface": True, "extends": "javax.microedition.io.StreamConnection",
        "methods": ["java.lang.String getType()", "java.lang.String getEncoding()",
                    "long getLength()"],
    },
    "javax/microedition/io/SocketConnection": {
        "interface": True, "extends": "javax.microedition.io.StreamConnection",
        "constants": {"DELAY": "byte:0", "LINGER": "byte:1", "KEEPALIVE": "byte:2",
                      "RCVBUF": "byte:3", "SNDBUF": "byte:4"},
        "methods": [
            "java.io.InputStream openInputStream()",
            "java.io.OutputStream openOutputStream()",
            "java.lang.String getAddress()", "int getPort()",
            "java.lang.String getLocalAddress()", "int getLocalPort()",
            "int getSocketOption(byte)", "void setSocketOption(byte, int)",
        ],
    },
    "javax/microedition/io/HttpConnection": {
        "interface": True, "extends": "javax.microedition.io.ContentConnection",
        "constants": {"GET": '"GET"', "POST": '"POST"', "HEAD": '"HEAD"'},
        "methods": [
            "java.lang.String getURL()", "java.lang.String getProtocol()",
            "java.lang.String getHost()", "int getPort()",
            "java.lang.String getFile()", "java.lang.String getQuery()",
            "java.lang.String getRef()",
            "void setRequestMethod(java.lang.String)",
            "java.lang.String getRequestMethod()",
            "void setRequestProperty(java.lang.String, java.lang.String)",
            "java.lang.String getRequestProperty(java.lang.String)",
            "int getResponseCode()", "java.lang.String getResponseMessage()",
            "java.lang.String getHeaderField(java.lang.String)",
            "java.lang.String getHeaderField(int)",
            "java.lang.String getHeaderFieldKey(int)",
            "long getDate()", "long getExpiration()", "long getLastModified()",
            "java.io.InputStream openInputStream()",
            "java.io.DataInputStream openDataInputStream()",
            "java.io.OutputStream openOutputStream()",
            "java.io.DataOutputStream openDataOutputStream()",
        ],
    },
    "javax/microedition/media/Player": {
        "interface": True,
        "constants": {"CLOSED": "0", "UNREALIZED": "100", "REALIZED": "200",
                      "PREFETCHED": "300", "STARTED": "400", "TIME_UNKNOWN": "-1"},
        "methods": [
            "void realize()", "void prefetch()", "void start()", "void stop()",
            "void deallocate()", "void close()", "void setLoopCount(int)",
            "void addPlayerListener(javax.microedition.media.PlayerListener)",
            "void removePlayerListener(javax.microedition.media.PlayerListener)",
            "int getState()", "long getDuration()", "long getMediaTime()",
            "javax.microedition.media.Control getControl(String)",
            "javax.microedition.media.Control[] getControls()",
        ],
    },
    "javax/microedition/media/Manager": {
        "constants": {"TONE_STOP": "1", "TONE_BLOCKING": "2",
                      "TONE_CONSTANT_DURATION": "4"},
        "methods": [
            "public static javax.microedition.media.Player createPlayer(java.io.InputStream, String)",
            "public static javax.microedition.media.Player createPlayer(String)",
            "public static String[] getSupportedContentTypes(String)",
            "public static String[] getSupportedProtocols(String)",
            "public static void playTone(int, int, int)",
        ],
    },
    "javax/microedition/media/Control": {
        "interface": True,
        "methods": ["javax.microedition.media.Control getControl(String)"],
    },
    # Controllable lives in javax.microedition.media (not .control) and is how
    # a game reaches a control without knowing the concrete type.
    "javax/microedition/media/Controllable": {
        "interface": True,
        "methods": ["javax.microedition.media.Control getControl(String)",
                    "javax.microedition.media.Control[] getControls()"],
    },
    "javax/microedition/media/control/ToneControl": {
        "interface": True,
        "methods": ["void setToneSequence(byte[])",
                    "byte[] getToneSequence()",
                    "void setSequence(byte[])"],
    },
    "javax/microedition/media/control/VolumeControl": {
        "interface": True,
        # MMAPI declares setLevel as returning int (the level actually set),
        # so a stub saying void makes every caller compile to setLevel(I)V and
        # the runtime - which declares setLevel(I)I - answers NoSuchMethodError.
        "methods": ["int setLevel(int)", "int getLevel()",
                    "void setMute(boolean)", "boolean isMuted()"],
    },
    # MIDIControl is the one control that changes a song while it plays.  The
    # stub carries the reference signatures, and the runtime registers the
    # same names with the same descriptors (getProgram twice, because the
    # reference implementations disagree about whether it returns the program
    # or {bank, program}).
    "javax/microedition/media/control/MIDIControl": {
        "interface": True,
        "constants": {"CONTROL_CHANGE": "176", "NOTE_ON": "144"},
        "methods": ["void setChannelVolume(int, int)",
                    "int getChannelVolume(int)",
                    "void setProgram(int, int, int)",
                    "int[] getProgram(int)",
                    "int[] getBankList(boolean)",
                    "int[] getProgramList(int)",
                    "String getProgramName(int, int)",
                    "String getKeyName(int, int, int)",
                    "boolean isBankQuerySupported()",
                    "void shortMidiEvent(int, int, int)",
                    "int longMidiEvent(byte[], int, int)"],
    },
    "javax/microedition/media/MediaException": {
        "extends": "Exception",
        "methods": ["MediaException()", "MediaException(String)"],
    },
    # The Nokia full-screen Canvas: GameCanvas's predecessor, and what almost
    # every pre-MIDP2 Series 40 game extends.  Without it the whole superclass
    # chain breaks and every INHERITED method reports as missing, which reads
    # like twenty missing APIs instead of one.
    "com/nokia/mid/ui/FullCanvas": {
        "extends": "javax.microedition.lcdui.Canvas", "abstract": True,
        "constants": {"SOFT_KEY_LEFT": "-6", "SOFT_KEY_MIDDLE": "-7",
                      "SOFT_KEY_RIGHT": "-8", "SOFT_KEY1": "-6",
                      "SOFT_KEY2": "-7", "SOFT_KEY3": "-8",
                      "KEY_SOFTKEY1": "-6", "KEY_SOFTKEY2": "-7",
                      "KEY_SOFTKEY3": "-8"},
        "methods": ["protected FullCanvas()",
                    "protected FullCanvas(boolean)"],
    },
    # The Nokia drawing extensions.  DirectGraphics is a Graphics in disguise -
    # DirectUtils.getDirectGraphics() hands the same object back - and it is how
    # a title writes pixels itself: 74 of the collected titles call it, 73 call
    # DirectUtils, and the two are what a sprite engine uses when it wants to
    # touch the frame buffer directly.
    "com/nokia/mid/ui/DirectGraphics": {
        "interface": True,
        "constants": {"FLIP_HORIZONTAL": "0x2000", "FLIP_VERTICAL": "0x4000",
                      "ROTATE_90": "90", "ROTATE_180": "180",
                      "ROTATE_270": "270",
                      "TYPE_BYTE_1_GRAY": "1", "TYPE_BYTE_1_GRAY_VERTICAL": "2",
                      "TYPE_BYTE_2_GRAY": "3", "TYPE_BYTE_4_GRAY": "4",
                      "TYPE_BYTE_8_GRAY": "5", "TYPE_BYTE_332_RGB": "6",
                      "TYPE_USHORT_4444_ARGB": "7", "TYPE_USHORT_444_RGB": "8",
                      "TYPE_USHORT_555_RGB": "9", "TYPE_USHORT_565_RGB": "10",
                      "TYPE_INT_888_RGB": "11", "TYPE_INT_8888_ARGB": "12"},
        "methods": [
            "void setARGBColor(int)",
            "int getNativePixelFormat()",
            "void drawImage(javax.microedition.lcdui.Image, int, int, int, int)",
            "void drawImage(int[], int, int, int, int)",
            "void drawTriangle(int, int, int, int, int, int, int)",
            "void fillTriangle(int, int, int, int, int, int, int)",
            "void drawPolygon(int[], int, int[], int, int, int)",
            "void fillPolygon(int[], int, int[], int, int, int)",
            "void drawPixels(short[], boolean, int, int, int, int, int, int, int, int)",
            "void drawPixels(int[], boolean, int, int, int, int, int, int, int, int)",
            "void drawPixels(byte[], byte[], int, int, int, int, int, int, int, int)",
            "void getPixels(short[], int, int, int, int, int, int, int)",
            "void getPixels(int[], int, int, int, int, int, int, int)",
            "void getPixels(byte[], byte[], int, int, int, int, int, int, int)",
            "int getAlphaComponent()",
        ],
    },
    "com/nokia/mid/ui/DirectUtils": {
        "methods": [
            "public static javax.microedition.lcdui.Image createImage(int, int)",
            "public static javax.microedition.lcdui.Image createImage(int, int, int)",
            "public static javax.microedition.lcdui.Image createImage(byte[], int, int)",
            "public static com.nokia.mid.ui.DirectGraphics getDirectGraphics(javax.microedition.lcdui.Graphics)",
        ],
    },
    # The side effects: vibration, the notification lights, the backlight.  Ten
    # titles call DeviceControl, and on a device every call is a side effect
    # with nothing to observe afterwards - which is exactly why a stub is
    # enough to compile against.
    "com/nokia/mid/ui/DeviceControl": {
        "methods": [
            "public static void setLights(int, int)",
            "public static void setLights(int, int, int)",
            "public static void flashLights(long)",
            "public static void flashLights(int)",
            "public static void vibrate(int)",
            "public static void startVibra(int, long)",
            "public static void stopVibra()",
            "public static void setScreenState(boolean)",
        ],
    },
    # The Nokia sound API: the Series 40 way to play a tone or a WAV before
    # MIDP 2.0's Manager existed.  Fourteen titles call it.
    "com/nokia/mid/sound/SoundListener": {
        "interface": True,
        "methods": ["void soundStateChanged(com.nokia.mid.sound.Sound, int)"],
    },
    "com/nokia/mid/sound/Sound": {
        "constants": {"FORMAT_TONE": "1", "FORMAT_WAV": "5",
                      "SOUND_PLAYING": "1", "SOUND_STOPPED": "0",
                      "SOUND_UNINITIALIZED": "-1"},
        "methods": [
            "public Sound(int, long)",
            "public Sound(byte[], int)",
            "public void init(int, long)",
            "public void init(byte[], int)",
            "public void play(int)",
            "public void stop()",
            "public void resume()",
            "public void release()",
            "public void setGain(int)",
            "public int getGain()",
            "public int getState()",
            "public void setSoundListener(com.nokia.mid.sound.SoundListener)",
            "public static int getConcurrentSoundCount(int)",
            "public static int[] getSupportedFormats()",
        ],
    },
    # The Nokia multipoint-touch extension.  One listener interface with two
    # callbacks: a device either has a multitouch panel or it does not, and a
    # Canvas that implements this and never receives a callback behaves exactly
    # like one on a single-touch phone.
    "com/nokia/mid/ui/multipointtouch/MultipointTouchListener": {
        "interface": True,
        # ONE method taking the array of active pointer ids - not three
        # pointer callback methods, which is what a first guess here produced
        # and which made a MIDlet that implements the interface fail to compile
        # for not overriding methods the real one does not have.
        "methods": ["void pointersChanged(int[])"],
    },
    "com/nokia/mid/ui/multipointtouch/MultipointTouch": {
        "methods": ["public static void setMultipointTouchListener(com.nokia.mid.ui.multipointtouch.MultipointTouchListener)",
                    "public static void setMultipointTouchListener(com.nokia.mid.ui.multipointtouch.MultipointTouchListener, int)"],
    },
    # The Nokia Series 40 file picker.  A UI service rather than an API the
    # runtime can satisfy: it opens a device dialog over the MIDlet.  Stubbed so
    # code that calls it compiles, and the runtime REFUSES it by name rather
    # than returning an empty selection that looks like an empty directory.
    "com/nokia/mid/ui/FileSelect": {
        "constants": {"FILE_SYSTEM_PHONE": "1", "FILE_SYSTEM_MMC": "2",
                      "FILE_SYSTEM_ALL": "3",
                      "MEDIA_TYPE_IMAGE": "1", "MEDIA_TYPE_VIDEO": "2",
                      "MEDIA_TYPE_AUDIO": "4", "MEDIA_TYPE_APPLICATION": "8",
                      "MEDIA_TYPE_ALL": "16"},
        # launch returns FileSelectDetail[], and FileSelectDetail exposes its
        # data as PUBLIC FIELDS (the caller writes `selected[0].url`), which is
        # read from the usage rather than guessed.
        "methods": ["public static com.nokia.mid.ui.FileSelectDetail[] launch(int, int, boolean)"],
    },
    "com/nokia/mid/ui/FileSelectDetail": {
        "methods": ["public FileSelectDetail()"],
        "fields": [["url", "Ljava/lang/String;"],
                   ["name", "Ljava/lang/String;"],
                   ["mimeType", "Ljava/lang/String;"],
                   ["size", "J"]],
    },
    "com/nokia/mid/ui/VirtualKeyboard": {
        "methods": ["public static void hideOpenKeypadCommand(boolean)",
                    "public static void showOpenKeypadCommand(boolean)",
                    "public static void setVisibility(boolean)"],
    },
    "javax/microedition/content/ContentException": {
        "extends": "Exception",
        "methods": ["ContentException()", "ContentException(String)"],
    },
    "javax/microedition/content/Invocation": {
        "methods": ["public Invocation(String, String, String)",
                    "public void setAction(String)", "public String getAction()",
                    "public void setArgs(String[])", "public String[] getArgs()",
                    "public void setResponseRequired(boolean)",
                    "public boolean getResponseRequired()"],
    },
    "javax/microedition/content/Registry": {
        "methods": ["public static javax.microedition.content.Registry getRegistry(String)",
                    "public void invoke(javax.microedition.content.Invocation)"],
    },
    # JSR 75.  FileConnection is an INTERFACE in MIDP and the runtime supplies
    # a builtin class of the same name, which works because invokeinterface
    # resolves on the receiver's class - the same arrangement as Player.
    "javax/microedition/io/file/ConnectionClosedException": {
        "extends": "java.io.IOException",
        "methods": ["public ConnectionClosedException()",
                    "public ConnectionClosedException(String)"],
    },
    "javax/microedition/io/file/IllegalModeException": {
        "extends": "RuntimeException",
        "methods": ["public IllegalModeException()",
                    "public IllegalModeException(String)"],
    },
    "javax/microedition/io/file/FileSystemListener": {
        "interface": True,
        "methods": ["void rootChanged(int, String)"],
    },
    "javax/microedition/io/file/FileSystemRegistry": {
        "methods": [
            "public static boolean addFileSystemListener(javax.microedition.io.file.FileSystemListener)",
            "public static boolean removeFileSystemListener(javax.microedition.io.file.FileSystemListener)",
            "public static java.util.Enumeration listRoots()",
        ],
    },
    "javax/microedition/io/file/FileConnection": {
        "interface": True,
        "extends": "javax.microedition.io.Connection",
        "methods": [
            "public long availableSize() throws java.io.IOException",
            "public boolean canRead()",
            "public boolean canWrite()",
            "public void create() throws java.io.IOException",
            "public void delete() throws java.io.IOException",
            "public boolean exists()",
            "public long fileSize() throws java.io.IOException",
            "public String getName()",
            "public String getPath()",
            "public String getURL()",
            "public boolean isDirectory()",
            "public boolean isHidden()",
            "public long lastModified() throws java.io.IOException",
            # list() returns directory entries WITH A TRAILING SLASH and file
            # entries without one - that is how a game tells them apart, since
            # the filter matches both.
            "public java.util.Enumeration list() throws java.io.IOException",
            "public java.util.Enumeration list(String, boolean) throws java.io.IOException",
            "public void mkdir() throws java.io.IOException",
            "public java.io.DataInputStream openDataInputStream() throws java.io.IOException",
            "public java.io.DataOutputStream openDataOutputStream() throws java.io.IOException",
            "public java.io.InputStream openInputStream() throws java.io.IOException",
            "public java.io.OutputStream openOutputStream() throws java.io.IOException",
            "public void setFileConnection(String) throws java.io.IOException",
            "public void setHidden(boolean) throws java.io.IOException",
            "public void setReadable(boolean) throws java.io.IOException",
            "public void setWritable(boolean) throws java.io.IOException",
            "public void truncate(int) throws java.io.IOException",
        ],
    },
    "javax/microedition/media/PlayerListener": {
        "interface": True,
        "methods": ["void playerUpdate(javax.microedition.media.Player, String, Object)"],
    },
    # ---- JSR 256, the sensor API -------------------------------------------
    # Zero of the 436 corpus titles use it; it is here because it is the only
    # standard way a MIDlet reaches an accelerometer, and a phone has one.
    "javax/microedition/sensor/SensorManager": {
        # A class, not an interface: the emitter writes only what it is given,
        # so every method here has to spell out "public static".
        "methods": [
            "public static javax.microedition.sensor.SensorInfo[] findSensors(java.lang.String)",
            "public static javax.microedition.sensor.SensorInfo[] findSensors(java.lang.String, java.lang.String)",
            "public static javax.microedition.sensor.SensorConnection getSensorConnection(javax.microedition.sensor.SensorInfo)",
            "public static void addSensorListener(javax.microedition.sensor.SensorListener, javax.microedition.sensor.SensorInfo)",
            "public static void addSensorListener(javax.microedition.sensor.SensorListener, java.lang.String)",
            "public static void removeSensorListener(javax.microedition.sensor.SensorListener)",
        ],
    },
    "javax/microedition/sensor/SensorInfo": {
        "interface": True,
        "constants": {
            "CONN_EMBEDDED": "1", "CONN_REMOTE": "2",
            "CONN_SHORT_RANGE_WIRELESS": "4", "CONN_WIRED": "8",
            "CONTEXT_TYPE_USER": '"user"', "CONTEXT_TYPE_AMBIENT": '"ambient"',
            "CONTEXT_TYPE_DEVICE": '"device"', "CONTEXT_TYPE_VEHICLE": '"vehicle"',
            "PROP_IS_CONTROLLABLE": '"controllable"',
            "PROP_IS_REPORTING_ERRORS": '"errorsReported"',
            "PROP_LATITUDE": '"latitude"', "PROP_LOCATION": '"location"',
            "PROP_LONGITUDE": '"longitude"', "PROP_MAX_RATE": '"maxSamplingRate"',
            "PROP_PERMISSION": '"permission"', "PROP_VENDOR": '"vendor"',
            "PROP_VERSION": '"version"',
        },
        "methods": [
            "java.lang.String getUrl()", "java.lang.String getQuantity()",
            "java.lang.String getContextType()", "java.lang.String getModel()",
            "int getConnectionType()",
            "javax.microedition.sensor.ChannelInfo[] getChannelInfos()",
            "int getMaxBufferSize()", "boolean isAvailable()",
            "boolean isAvailabilityPushSupported()",
            "boolean isConditionPushSupported()",
            "java.lang.String getProperty(java.lang.String)",
            "java.lang.String[] getPropertyNames()",
            "java.lang.String getDescription()",
        ],
    },
    "javax/microedition/sensor/SensorConnection": {
        "interface": True, "extends": "javax.microedition.io.Connection",
        "constants": {"STATE_OPENED": "1", "STATE_LISTENING": "2",
                      "STATE_CLOSED": "4"},
        "methods": [
            "javax.microedition.sensor.SensorInfo getSensorInfo()",
            "javax.microedition.sensor.Data[] getData(int)",
            "javax.microedition.sensor.Data[] getData(int, long, boolean, boolean, boolean)",
            "void setDataListener(javax.microedition.sensor.DataListener, int)",
            "void setDataListener(javax.microedition.sensor.DataListener, int, long, boolean, boolean, boolean)",
            "void removeDataListener()",
            "javax.microedition.sensor.Channel getChannel(javax.microedition.sensor.ChannelInfo)",
            "int[] getErrorCodes()", "java.lang.String getErrorText(int)",
            "int getState()",
        ],
    },
    "javax/microedition/sensor/ChannelInfo": {
        "interface": True,
        "constants": {"TYPE_DOUBLE": "1", "TYPE_INT": "2", "TYPE_OBJECT": "4"},
        "methods": [
            "java.lang.String getName()", "int getDataType()", "float getScale()",
            "java.lang.String getUnit()", "float getAccuracy()",
            "javax.microedition.sensor.MeasurementRange[] getMeasurementRanges()",
        ],
    },
    "javax/microedition/sensor/MeasurementRange": {
        "methods": [
            "public MeasurementRange(float, float, float)",
            "public float getSmallestValue()", "public float getLargestValue()",
            "public float getResolution()",
        ],
    },
    "javax/microedition/sensor/Data": {
        "interface": True,
        "methods": [
            "javax.microedition.sensor.ChannelInfo getChannelInfo()",
            "double[] getDoubleValues()", "int[] getIntValues()",
            "java.lang.Object[] getObjectValues()", "long getTimestamp(int)",
            "float getUncertainty(int)", "boolean isValid(int)",
        ],
    },
    "javax/microedition/sensor/DataListener": {
        "interface": True,
        "methods": ["void dataReceived(javax.microedition.sensor.SensorConnection, javax.microedition.sensor.Data[], boolean)"],
    },
    "javax/microedition/sensor/DataAndErrorListener": {
        "interface": True, "extends": "javax.microedition.sensor.DataListener",
        "methods": ["void errorReceived(javax.microedition.sensor.SensorConnection, int, long)"],
    },
    "javax/microedition/sensor/SensorListener": {
        "interface": True,
        "methods": ["void sensorAvailable(javax.microedition.sensor.SensorInfo)",
                    "void sensorUnavailable(javax.microedition.sensor.SensorInfo)"],
    },
    "javax/microedition/sensor/Channel": {
        "interface": True,
        "methods": [
            "javax.microedition.sensor.ChannelInfo getChannelInfo()",
            "java.lang.String getChannelUrl()",
            "void addCondition(javax.microedition.sensor.ConditionListener, javax.microedition.sensor.Condition)",
            "void removeCondition(javax.microedition.sensor.ConditionListener, javax.microedition.sensor.Condition)",
            "javax.microedition.sensor.Condition[] getConditions(javax.microedition.sensor.ConditionListener)",
            "void removeAllConditions()",
            "void removeConditionListener(javax.microedition.sensor.ConditionListener)",
            # Not in the 1.2 javadoc: this runtime keeps the sample rate on the
            # channel object rather than behind a SampleRateControl.
            "void setSampleRate(float)", "float getSampleRate()",
            "float[] getSampleRates()",
        ],
    },
    "javax/microedition/sensor/Condition": {
        "interface": True,
        "constants": {"OP_EQUALS": '"eq"', "OP_GREATER_THAN": '"gt"',
                      "OP_GREATER_THAN_OR_EQUALS": '"ge"',
                      "OP_LESS_THAN": '"lt"',
                      "OP_LESS_THAN_OR_EQUALS": '"le"'},
        "methods": ["boolean isMet(double)", "boolean isMet(java.lang.Object)"],
    },
    "javax/microedition/sensor/ConditionListener": {
        "interface": True,
        "methods": ["void conditionMet(javax.microedition.sensor.SensorConnection, javax.microedition.sensor.Data, javax.microedition.sensor.Condition)"],
    },

    # ---- javax.bluetooth (JSR 82) -------------------------------------
    "javax/microedition/io/StreamConnectionNotifier": {
        "interface": True,
        "extends": "javax.microedition.io.Connection",
        "methods": [
            "javax.microedition.io.StreamConnection acceptAndOpen()",
        ],
    },
    "javax/bluetooth/BluetoothStateException": {
        "extends": "java.io.IOException",
        "methods": ["public BluetoothStateException()",
                    "public BluetoothStateException(String)"],
    },
    "javax/bluetooth/UUID": {
        "methods": [
            "public UUID(String)",
            "public UUID(long)",
            "public boolean equals(Object)",
            "public int hashCode()",
            "public String toString()",
        ],
    },
    "javax/bluetooth/DeviceClass": {
        "methods": [
            "public int getMajorDeviceClass()",
            "public int getMinorDeviceClass()",
            "public int getServiceClasses()",
        ],
    },
    "javax/bluetooth/DataElement": {
        "constants": {
            "NULL": "0", "U_INT_1": "8", "U_INT_2": "9", "U_INT_4": "10",
            "U_INT_8": "11", "U_INT_16": "12", "INT_1": "16", "INT_2": "17",
            "INT_4": "18", "INT_8": "19", "INT_16": "20", "UUID": "24",
            "STRING": "32", "BOOL": "40", "DATSEQ": "48", "DATALT": "56",
            "URL": "64",
        },
        "methods": [
            "public DataElement(int)",
            "public DataElement(boolean)",
            "public DataElement(int, long)",
            "public DataElement(int, Object)",
            "public int getDataType()",
            "public long getLong()",
            "public boolean getBoolean()",
            "public Object getValue()",
            "public int getSize()",
            "public void addElement(DataElement)",
            "public void insertElementAt(DataElement, int)",
            "public boolean removeElement(DataElement)",
        ],
    },
    "javax/bluetooth/ServiceRecord": {
        "interface": True,
        "constants": {
            "NOAUTHENTICATE_NOENCRYPT": "0",
            "AUTHENTICATE_NOENCRYPT": "1",
            "AUTHENTICATE_ENCRYPT": "2",
        },
        "methods": [
            "DataElement getAttributeValue(int)",
            "String getConnectionURL(int, boolean)",
            "RemoteDevice getHostDevice()",
            "int[] getAttributeIDs()",
            "boolean populateRecord(int[])",
            "void setAttributeValue(int, DataElement)",
            "void removeAttributeValue(int, boolean)",
        ],
    },
    "javax/bluetooth/RemoteDevice": {
        "methods": [
            "public static RemoteDevice getRemoteDevice(String)",
            "public String getBluetoothAddress()",
            "public String getFriendlyName(boolean)",
            "public DeviceClass getDeviceClass()",
            "public boolean equals(Object)",
            "public int hashCode()",
            "public boolean authenticate()",
            "public boolean authorize(String)",
            "public boolean encrypt(boolean)",
            "public boolean isAuthenticated()",
            "public boolean isEncrypted()",
            "public boolean isTrustedDevice()",
        ],
    },
    "javax/bluetooth/LocalDevice": {
        "methods": [
            "public static LocalDevice getLocalDevice()",
            "public String getFriendlyName()",
            "public String getBluetoothAddress()",
            "public DeviceClass getDeviceClass()",
            "public DiscoveryAgent getDiscoveryAgent()",
            "public String getProperty(String)",
            "public ServiceRecord getRecord(ServiceRecord)",
            "public void updateRecord(ServiceRecord)",
            "public boolean setDiscoverable(int)",
        ],
    },
    "javax/bluetooth/DiscoveryAgent": {
        "constants": {
            "NOT_DISCOVERABLE": "0", "GIAC": "10390323", "LIAC": "10390272",
            "CACHED": "0", "PREKNOWN": "1",
        },
        "methods": [
            "public boolean startInquiry(int, DiscoveryListener)",
            "public boolean cancelInquiry(DiscoveryListener)",
            "public int searchServices(int[], UUID[], RemoteDevice, DiscoveryListener)",
            "public boolean cancelServiceSearch(int)",
            "public RemoteDevice[] retrieveDevices(int)",
            "public String selectService(UUID, int, boolean)",
        ],
    },
    "javax/bluetooth/DiscoveryListener": {
        "interface": True,
        "constants": {
            "INQUIRY_COMPLETED": "0", "INQUIRY_TERMINATED": "1",
            "INQUIRY_ERROR": "2", "SERVICE_SEARCH_COMPLETED": "1",
            "SERVICE_SEARCH_TERMINATED": "2", "SERVICE_SEARCH_ERROR": "3",
            "SERVICE_SEARCH_NO_RECORDS": "4",
            "SERVICE_SEARCH_DEVICE_NOT_REACHABLE": "6",
        },
        "methods": [
            "void deviceDiscovered(RemoteDevice, DeviceClass)",
            "void servicesDiscovered(int, ServiceRecord[])",
            "void serviceSearchCompleted(int, int)",
            "void inquiryCompleted(int)",
        ],
    },
    "javax/bluetooth/L2CAPConnection": {
        "interface": True,
        "extends": "javax.microedition.io.Connection",
        "constants": {"DEFAULT_MTU": "672", "MINIMUM_MTU": "48"},
        "methods": [
            "void send(byte[])",
            "int receive(byte[])",
            "boolean ready()",
            "int getReceiveMTU()",
            "int getTransmitMTU()",
            "void close()",
        ],
    },
    "javax/bluetooth/L2CAPConnectionNotifier": {
        "interface": True,
        "extends": "javax.microedition.io.Connection",
        "methods": ["L2CAPConnection acceptAndOpen()"],
    },
}

RETURNING = {
    "getWidth": "int", "getHeight": "int", "getX": "int", "getY": "int",
    "size": "int", "getCell": "int", "isSelected": "boolean",
}

HEADER = """/* GENERATED by tools/midp_stubs.py - do not edit.
 *
 * MIDP/CLDC signature stub.  Used ONLY to compile MIDlets; never packaged.
 * Every body throws, so a stub that accidentally reaches a runtime classpath
 * fails loudly instead of silently doing nothing.
 */
"""


def _named_params(params):
    """Turn "String, int, java.util.TimeZone" into "String p0, int p1, ...".

    Java needs parameter NAMES in a method that has a body, and writing them by
    hand for 200 signatures would be 200 chances to typo one.
    """
    if not params.strip():
        return ""
    out = []
    for i, t in enumerate(params.split(",")):
        out.append("%s p%d" % (t.strip(), i))
    return ", ".join(out)


def _split_sig(sig):
    """Split "public void foo(int) throws X" into (head, params, tail).

    The TAIL is what follows the closing parenthesis, which in Java is the
    throws clause.  It used to be discarded, and that is not cosmetic: a stub
    without `throws RecordStoreFullException` makes a real game uncompilable,
    because javac rejects a catch for an exception the try body cannot throw -
    and catching that one is what every game that saves a high score does.

    The stub file is the contract a game is compiled against, so a stub that is
    not faithful to the real API rejects working games, which is the opposite of
    what a compatibility generator is for.
    """
    if "(" not in sig:
        return sig, None, ""
    i = sig.index("(")
    j = sig.rindex(")")
    return sig[:i] + "(", sig[i + 1:j], sig[j + 1:]


def _java_type(desc):
    """A JVM type descriptor as it is written in Java source."""
    if desc.startswith("L") and desc.endswith(";"):
        return desc[1:-1].replace("/", ".").split(".")[-1]
    if desc.startswith("["):
        return _java_type(desc[1:]) + "[]"
    return {"I": "int", "J": "long", "Z": "boolean", "B": "byte",
            "C": "char", "S": "short", "F": "float", "D": "double",
            "V": "void"}.get(desc, "int")


def emit(path, name, spec, out_root):
    pkg = os.path.dirname(path).replace("/", ".")
    cls = os.path.basename(path)
    lines = [HEADER]
    if pkg:
        lines.append("package %s;\n" % pkg)
    # Wildcard imports of the MIDP packages: harmless when unused, and they
    # save having to track which stub refers to which other stub by hand.
    lines.append("import javax.microedition.lcdui.*;\n")
    lines.append("import javax.microedition.lcdui.game.*;\n")
    lines.append("import javax.microedition.midlet.*;\n")
    lines.append("@SuppressWarnings(\"all\")\n")
    kind = "interface" if spec.get("interface") else (
        "abstract class" if spec.get("abstract") else "class")
    ext = ""
    # Interfaces extend interfaces with the SAME keyword in Java, and this used
    # to skip them: `if not spec.get("interface")` meant an interface's parent
    # was silently dropped.  JSR 75's FileConnection extends Connection, so
    # close() - and every other inherited method - was missing from the stub,
    # and a MIDlet that called it did not compile.  Same class of bug as the
    # dropped throws clause: the stub has to be faithful or it rejects working
    # code.
    if spec.get("extends"):
        ext = " extends " + spec["extends"]
    lines.append("public %s %s%s {\n" % (kind, cls, ext))

    # Public FIELDS.  A stub that only declares methods cannot express a class
    # whose data is read directly - Nokia's FileSelectDetail is one, and the
    # caller writes `selected[0].url`.  The declaration is written with a JVM
    # descriptor because that is what the rest of this table uses, so it is
    # translated here rather than making the caller write Java.
    for fname, fdesc in (spec.get("fields") or []):
        lines.append("    public %s %s;\n" % (_java_type(fdesc), fname))

    for cname, cval in (spec.get("constants") or {}).items():
        if cval.startswith("null:"):
            # A TYPED null constant.  `public static final Object X = null` is
            # not the same field as `public static final Command X = null`, and
            # the runtime is entitled to the latter - a MIDlet then fails with
            # NoSuchFieldError naming a descriptor it never wrote.
            lines.append("    public static final %s %s = null;\n"
                         % (_java_type(cval[5:]), cname))
        elif cval == "null":
            lines.append("    public static final Object %s = null;\n" % cname)
        elif cval.startswith('"'):
            # A String constant (HttpConnection.GET/POST/HEAD).  javac inlines
            # it at every use site, so the runtime's field is never read - but
            # the fixture must still compile against the same API a MIDlet sees.
            lines.append("    public static final String %s = %s;\n" % (cname, cval))
        elif cval.startswith("byte:"):
            # A TYPED constant.  JSR 118 declares SocketConnection's option ids
            # as `byte`, and it matters at the use site rather than in the
            # runtime: javac inlines the value AND its type, so a fixture built
            # against an `int` KEEPALIVE hands setSocketOption(byte,int) an int
            # and does not compile - while the same code against the real API
            # compiles.  The stub has to hold the same type the spec does.
            lines.append("    public static final byte %s = %s;\n"
                         % (cname, cval[5:]))
        else:
            lines.append("    public static final int %s = %s;\n" % (cname, cval))

    for m in spec.get("methods", []):
        if spec.get("interface"):
            # Java requires parameter names even in an interface declaration.
            head, params, tail = _split_sig(m.replace("public ", ""))
            lines.append("    %s%s)%s;\n"
                         % (head, _named_params(params), tail))
            continue
        if "(" not in m:
            # A bare constant declaration such as "public static final int X".
            lines.append("    %s = 0;\n" % m)
            continue
        head, params, tail = _split_sig(m)
        if "abstract" in head or spec.get("interface"):
            # An abstract method declares; it does not have a body.
            lines.append("    %s%s)%s;\n"
                         % (head, _named_params(params), tail))
        else:
            lines.append("    %s%s)%s { throw new RuntimeException(\"midp stub\"); }\n"
                         % (head, _named_params(params), tail))
    lines.append("}\n")

    rel = os.path.join(out_root, path + ".java")
    os.makedirs(os.path.dirname(rel), exist_ok=True)
    with open(rel, "w") as f:
        f.write("".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None,
                    help="build directory (default: <repo>/build/midp-stubs)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = args.out or os.path.join(root, "build", "midp-stubs")
    src = os.path.join(out, "src")
    classes = os.path.join(out, "classes")

    shutil.rmtree(src, ignore_errors=True)
    os.makedirs(classes, exist_ok=True)

    for path, spec in CLASSES.items():
        emit(path, os.path.basename(path), spec, src)

    sources = []
    for dirpath, _dirs, files in os.walk(src):
        for f in files:
            if f.endswith(".java"):
                sources.append(os.path.join(dirpath, f))

    if not args.quiet:
        sys.stderr.write("compiling %d MIDP stub sources...\n" % len(sources))

    if shutil.which("javac") is None:
        sys.exit("javac not found; cannot build the MIDP stubs")

    # -proc:none: the stubs need no annotation processing and it speeds the
    # build up considerably.
    cmd = ["javac", "-proc:none", "-nowarn", "-d", classes] + sorted(sources)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("stub compilation failed")

    # Classpath only: these must never be packaged into a game JAR.
    print(classes)


if __name__ == "__main__":
    main()
