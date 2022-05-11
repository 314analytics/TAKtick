TAKtick
=======

TAK     (tăk)     : Team Awareness Kit</br>
tick    (tĭk)     : a bloodsucking arachnid</br>
TAKtick (tăk′tĭk) : straightforward CoT/TAK TCP server</br>

This multi-platform command-line tool was born out of frustration with overly complex 'TAK server' and 'Cursor on Target router' solutions that have everything including the kitchen sink, would require installing a cornucopia of unknown extra libraries and software onto your PC, and then require a bevy of configuration settings once you hopefully managed to get the darn thing installed.

This is TCP only, does not support SSL, and is not intended for a production environment.  However, when you just want something basic to do some TAK or CoT testing without a lot of fuss, perhaps this might suffice.

## Usage

```
TAKtick 8089
```

where '8089' is the TCP port that you want the server to bind to and listen for incoming connections.  Press 'Q' to stop the server, or press any other key for it to display how many participants are currently connected to the server.

Every CoT message received from any participant is repeated to all participants.

## ATAK configuration

![ATAK screenshot](https://user-images.githubusercontent.com/86503169/135726814-30a4067b-7099-4d68-abfd-1bf04584b6ca.png)

In the above image, '192.168.10.20' is the IP address of a network interface on the server, and '8089' is the TCP port that you've told TAKtick to use.  The Name field is shown as 'TAKtick example', but can be anything you want it to be.  Be sure to check the 'Advanced Options' checkbox so that you can pick the Streaming Protocol to be 'TCP'.

