### Hiện tại bộ gõ này không còn được hỗ trợ. Do đó, nếu bạn gặp vấn đề trong quá trình sử dụng thì có thể xem xét sử dụng một trong các tùy chọn sau:
- [ibus-bamboo](https://github.com/BambooEngine/ibus-bamboo)
- [ibus-bogo](https://github.com/BoGoEngine/ibus-bogo)
- Một trong các bản fork của ibus-unikey https://github.com/vn-input/ibus-unikey/network/members
- Hoặc các bộ gõ khác...

IBus-Unikey IME
===============

[![Build Status](https://travis-ci.org/vn-input/ibus-unikey.svg?branch=master)](https://travis-ci.org/vn-input/ibus-unikey)

ibus-unikey is an [IBus](https://github.com/ibus/ibus) IME.
It use Unikey-engine for progress key event.
(a modified version of it)

# Install From SourceIn (Guide)

### Ubuntu

1. Install required packages:

    ```
    sudo apt-get install -y cmake g++ make pkg-config libibus-1.0-dev libgtk-3-dev gettext
    ```
2. Build & install

    ```
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=release -DLIBEXECDIR=/usr/lib/ibus ..
    sudo make
    sudo make install
    ```

3. Apply the keyboard input method


    #### Step 1: Install Vietnamese Language Support

    > You must have the "Language Support" application installed.
    
    - Open the Language Support application.
    - In the list of available languages, locate Vietnamese.
    - If Vietnamese is not installed, select it and click Install.
    - Change your keyboard input method system to IBus. (The exact steps for this may vary depending on your operating system)
   
    #### Step 2: Configure IBus for Vietnamese
    
    - Open a terminal window.
    - Run the command ibus-setup.
    - Navigate to the Input Method tab.
    - If Vietnamese - Unikey is not listed, click Add and select it.
    - Close the ibus-setup window.
    - In the same terminal window, run the command ibus restart.
   
    #### Step 3: Select IBus in System Settings
    
    - Run im-config in the terminal.
    - When prompted for "user configuration", select ibus.

   #### Step 4: Finalize Setup
    
    - Restart your computer.
    - Open your system Settings.
    - Navigate to the input method settings. (This location may vary depending on your operating system)
    - Change the input method to Vietnamese.

    > You should now be able to type in Vietnamese!
    


### FreeBSD

This instruction is for FreeBSD 12-RELEASE.

Build and install:

    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=release -DLIBEXECDIR=/usr/local/lib ..
    make
    sudo make install

If you use GNOME, `ibus-unikey` should work once IBus is configured correctly.

If you use KDE, some additional configuration is needed:

1. Add

    ```
    export GTK_IM_MODULE=ibus
    export XMODIFIERS=@im=ibus
    export QT_IM_MODULE=ibus
    ```
 into `$HOME/.profile`.

2. Make `ibus-daemon` start automatically with the right configuration:

    ```
    $ echo "ibus-daemon -d -x -r -n kde" > $HOME/.kde/Autostart/ibus-daemon-autostart.sh && chmod +x $HOME/.kde/Autostart/ibus-daemon-autostart.sh
    ```

