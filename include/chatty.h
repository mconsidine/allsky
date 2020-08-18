// chatty.h
// enable/disable printing debug information from individual files
// written by Jan Soldan

	
#ifndef _CHATTY_H_
#define _CHATTY_H_

//#define QHY_LOGGER

#ifdef QHY_LOGGER

#define CHATTY_CMOSDLL
//#define CHATTY_CYUSB
//#define CHATTY_UNLOCK_IMAGE_QUEUE
//#define CHATTY_DOWNLOAD_FX3

#define CHATTY_X2CAMERA
#define CHATTY_QHYCCD
#define CHATTY_QHYCAM
#define CHATTY_QHYBASE

#define CHATTY_QHY5IIBASE
#define CHATTY_QHY5LIIBASE
#define CHATTY_QHY5LIIM
#define CHATTY_QHY5LIIC
#define CHATTY_QHY5TIIC
#define CHATTY_QHY5PIIC

#define CHATTY_QHY10

#define CHATTY_QHY5IIIBASE
#define CHATTY_QHY5IIICOOLBASE
#define CHATTY_QHY5IIIDDRCOOLBASE

#define CHATTY_QHY5III128BASE
#define CHATTY_QHY128C

#define CHATTY_QHY5III163BASE
#define CHATTY_QHY5III165BASE
#define CHATTY_QHY5III168BASE

#define CHATTY_QHY5III174BASE
#define CHATTY_QHY5III174C
#define CHATTY_QHY5III174M

#define CHATTY_QHY5III178BASE
#define CHATTY_QHY5III178COOLBASE
#define CHATTY_QHY5III178C

#define CHATTY_QHY5III183BASE
#define CHATTY_QHY183
#define CHATTY_QHY183C

#define CHATTY_QHY5III185BASE
#define CHATTY_QHY5III185C

#define CHATTY_QHY5III224BASE
#define CHATTY_QHY5III224COOLBASE
#define CHATTY_QHY5III224C

#define CHATTY_QHY5III247BASE

#define CHATTY_QHY5III290BASE
#define CHATTY_QHY5III290COOLBASE
#define CHATTY_QHY5III290C
#define CHATTY_QHY5III290M

#define CHATTY_QHY5III367BASE

#endif // QHY_LOGGER

#endif // _CHATTY_H_

