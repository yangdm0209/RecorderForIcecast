#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <jni.h>
#include <pthread.h>

#include "libmp3lame/lame.h"
#include "shout/shout.h"
#include "ringbuf.h"
#include "log.h"

#define BUFFER_SIZE 20480
#define SEND_BUFFER_SIZE 4096

#define EXCEPTION_RECORD_ERROR -1
#define EXCEPTION_ENCODE_ERROR -2
#define EXCEPTION_MEMORY_ERROR -3
#define EXCEPTION_NETWORK_ERROR -4

/**
 * Global
 */
static JavaVM *g_jvm = NULL;
static jobject g_feed = NULL;

static int g_initialize = 0;

static int THREAD_RUNNING = 1; // thread is running
static int THREAD_STOPING = 2; // thread is stoping
static int THREAD_STOPED = 3; // thread has stoped

static int g_record_running = 0;
static int g_send_running = 0;

static lame_t g_lame = NULL;
static shout_t *g_shout = NULL;

static pthread_t g_record_tid = 0;
static pthread_t g_send_tid = 0;

static ringbuf g_data_buf;

int throw_exception(JNIEnv *jenv, int code, const char* str) {
	int ret = -1;
	jclass cls;
	jmethodID mid;
	jstring jstr;

	JNIEnv *env = jenv;

	if (NULL == jenv) {
		if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
			LOGE("AttachCurrentThread() failed");
			return ret;
		}
	}

	do {
		// find class
		cls = (*env)->GetObjectClass(env, g_feed);
		if (cls == NULL) {
			LOGE("FindClass() failed");
			break;
		}

		// get exception method to
		mid = (*env)->GetMethodID(env, cls, "exception",
				"(ILjava/lang/String;)V");
		if (mid == NULL) {
			LOGE("GetMethodID() exception failed");
			break;
		}

		jstr = (*env)->NewStringUTF(env, str);
		(*env)->CallNonvirtualVoidMethod(env, g_feed, cls, mid, code, jstr);
		(*env)->DeleteLocalRef(env, jstr);
		ret = 0;

	} while (0);

	if (NULL == jenv) {
		if ((*g_jvm)->DetachCurrentThread(g_jvm) != JNI_OK) {
			LOGE("DetachCurrentThread() failed");
		}
	}

	return ret;
}

int init(const unsigned char *server, int port, const unsigned char *mount,
		const unsigned char *user, const unsigned char *passwd, int sampleRate,
		int channel, int brate, int vid, const unsigned char* auth) {

	LOGI(
			"init para is server:%s, port:%d, mount:%s, user:%s, passwd:%s, samepleRate:%d, channel:%d, brate:%d",
			server, port, mount, user, passwd, sampleRate, channel, brate);

	int ret = -1;

	if (0 != g_initialize) {
		return 1;
	}

	// init lame
	g_lame = lame_init();
	if (NULL == g_lame) {
		LOGE("Java VM is NULL !!!!!!!!!!!!!");
		return ret;
	}
	lame_set_num_channels(g_lame, channel);
	lame_set_in_samplerate(g_lame, sampleRate);
	lame_set_brate(g_lame, brate);
	lame_set_mode(g_lame, 1);
	lame_set_quality(g_lame, 2);
	lame_init_params(g_lame);

	do {
		// init shout
		shout_init();

		if (!(g_shout = shout_new(sampleRate, brate, channel))) {
			LOGE("Create shout failed");
			break;
		}
		// set option
		if (shout_set_host(g_shout, server) != SHOUTERR_SUCCESS) {
			LOGE("set server to shout failed");
			break;
		}
		if (shout_set_protocol(g_shout, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS) {
			LOGE("set protocol to shout failed");
			break;
		}
		if (shout_set_port(g_shout, port) != SHOUTERR_SUCCESS) {
			LOGE("set port to shout failed");
			break;
		}
		if (shout_set_password(g_shout, passwd) != SHOUTERR_SUCCESS) {
			LOGE("set password to shout failed");
			break;
		}
		if (shout_set_mount(g_shout, mount) != SHOUTERR_SUCCESS) {
			LOGE("set mount point to shout failed");
			break;
		}
		if (shout_set_user(g_shout, user) != SHOUTERR_SUCCESS) {
			LOGE("set user to shout failed");
			break;
		}
		if (shout_set_format(g_shout, SHOUT_FORMAT_MP3) != SHOUTERR_SUCCESS) {
			LOGE("set format_mp3 to shout failed");
			break;
		}
		if (shout_set_interval(g_shout, 16000) != SHOUTERR_SUCCESS) {
			LOGE("set interval to shout failed");
			break;
		}
		if (shout_set_vid(g_shout, vid) != SHOUTERR_SUCCESS) {
			LOGE("set vid to shout failed");
			break;
		}
		if (shout_set_auth(g_shout, auth) != SHOUTERR_SUCCESS) {
			LOGE("set auth to shout failed");
			break;
		}
		if (shout_upate_sts(g_shout, SHOUT_UP_STATUS_START) != SHOUTERR_SUCCESS) {
			LOGE("set start status error");
			break;
		}

		if (shout_open(g_shout) != SHOUTERR_SUCCESS) {
			LOGE("Shout open error");
			break;
		}

		ret = 0;
	} while (0);

	if (0 > ret) {
		shout_free(g_shout);
		g_shout = NULL;
	} else {
		g_initialize = 1;
	}

	return ret;
}

void release_lame() {
	if (NULL != g_lame) {
		lame_close(g_lame);
		g_lame = NULL;
	}
}

void release_shout() {
	if (g_shout != NULL) {
		shout_close(g_shout);
		shout_free(g_shout);
		shout_shutdown();
		g_shout = NULL;
	}
}

int stop(int stop_by_user) {
	LOGI("Call stop function");
	void* thread_val;

	if (1 == stop_by_user) {
		shout_upate_sts(g_shout, SHOUT_UP_STATUS_STOP);
	}

	g_record_running = THREAD_STOPING;
	g_send_running = THREAD_STOPING;

	pthread_join(g_record_tid, &thread_val);
	pthread_join(g_send_tid, &thread_val);

	// release shout and lame
	release_shout();
	release_lame();
	g_initialize = 0;

	// clear buffer
	rb_free(&g_data_buf);
	LOGI("End stop function");
}

void* record_thread(void *arg) {
	JNIEnv *env;
	jclass cls;
	jmethodID mid;
	int recvLen, encodeLen;
	jshort recvBuf[BUFFER_SIZE];
	jshortArray pcmArray;
	unsigned char encodeBuf[BUFFER_SIZE];

	LOGI("Start record thread");

	if (NULL == g_jvm) {
		LOGE("Cann't get Java VM!!!!!!!!!!!!!");
		return NULL;
	}

	// Attach thread
	if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
		LOGE("AttachCurrentThread() failed");
		return NULL;
	}
	// find class
	cls = (*env)->GetObjectClass(env, g_feed);
	if (cls == NULL) {
		LOGE("FindClass() failed");
		goto error;
	}

	// get readPCMData method to read pcm data 
	// int readPCMData(short[] buffer, int buferLen) <--> ([SI)I
	mid = (*env)->GetMethodID(env, cls, "readPCMData", "([SI)I");
	if (mid == NULL) {
		LOGE("GetMethodID() readPCMData failed");
		goto error;
	}

	// new jshortArray and get jsort*
	pcmArray = (*env)->NewShortArray(env, BUFFER_SIZE);

	while (g_record_running == THREAD_RUNNING) {
		recvLen = 0;
		encodeLen = 0;

		// read pcm data
		recvLen = (*env)->CallNonvirtualIntMethod(env, g_feed, cls, mid,
				pcmArray, BUFFER_SIZE);
		memset(recvBuf, 0, sizeof(recvBuf));
		(*env)->GetShortArrayRegion(env, pcmArray, 0, recvLen, recvBuf);

		LOGI("Recv pcm data len: %d", recvLen);

		if (recvLen > 0) {
			// encode pcm data to MP3
			memset(encodeBuf, 0, sizeof(encodeBuf));
			encodeLen = lame_encode_buffer(g_lame, recvBuf, recvBuf, recvLen,
					encodeBuf, BUFFER_SIZE);
			LOGI("Encode mp3 length: %d, ret: %d", recvLen, encodeLen);

			if (encodeLen >= 0) {
				// save data to ringbuffer, send_thread will send
				rb_write(&g_data_buf, encodeBuf, encodeLen);
				LOGI("Save data to buf, data len: %d, buf have filled len: %d",
						encodeLen, rb_filled(&g_data_buf));
			} else {
				LOGE("encode PCM failed");
				throw_exception(env, EXCEPTION_ENCODE_ERROR, "Encode failed");
			}
		} else {
			LOGE("Can not record, ret len is: %d", recvLen);
			throw_exception(env, EXCEPTION_RECORD_ERROR, "Record failed");
			break;
		}

	}

	(*env)->DeleteLocalRef(env, pcmArray);

	error:

	g_record_running = THREAD_STOPED;

	//Detach thread
	if ((*g_jvm)->DetachCurrentThread(g_jvm) != JNI_OK) {
		LOGE("DetachCurrentThread failed");
	}

	LOGI("End record thread");
	g_record_tid = 0;
	pthread_exit(0);
}
void* send_thread(void *arg) {
	int recvLen, sendRet;
	unsigned char recvBuf[SEND_BUFFER_SIZE + 1];

	LOGI("Start send thread");

	while (g_send_running == THREAD_RUNNING) {
		recvLen = 0;

		// do not send until PCM data length is 4096
		if (rb_filled(&g_data_buf) >= 4096) {
			// get PCM data from ringbuffer
			recvLen = rb_read(&g_data_buf, recvBuf, SEND_BUFFER_SIZE);
			LOGI("Get MP3 data length: %d", recvLen);

			if (recvLen > 0) {
				// send MP3 data to server
				sendRet = shout_send(g_shout, recvBuf, recvLen);
				LOGI("Send to Server length: %d, ret: %d", recvLen, sendRet);

				if (sendRet < 0) {
					LOGE("Send data to ICE-Server failed, ret=%d", sendRet);
					if (SHOUTERR_UNCONNECTED == sendRet)
						throw_exception(NULL, EXCEPTION_NETWORK_ERROR,
								"Have not connect server");
					else if (SHOUTERR_SOCKET == sendRet)
						throw_exception(NULL, EXCEPTION_NETWORK_ERROR,
								"Net work error");
					else if (SHOUTERR_MALLOC == sendRet)
						throw_exception(NULL, EXCEPTION_MEMORY_ERROR,
								"Memory not enough");
					else
						throw_exception(NULL, EXCEPTION_NETWORK_ERROR,
								"Net work error undefined");
					break;
				}

			} // if (recvLen > 0)

		} // if (rb_filled(&g_data_buf) > BUFFER_SIZE)

	} // while (g_encode_running == THREAD_RUNNING)

	g_send_running = THREAD_STOPED;

	LOGI("End send thread");
	g_send_tid = 0;
	pthread_exit(0);
}

int init_wrapper(JNIEnv *env, jstring jserver, jint jport, jstring jmount,
		jstring juser, jstring jpasswd, jint jsampleRate, jint jchannel,
		jint jbrate, jint jvid, jstring jauth) {
	const unsigned char *server = NULL;
	const unsigned char *mount = NULL;
	const unsigned char *user = NULL;
	const unsigned char *passwd = NULL;
	const unsigned char *auth = NULL;
	const int port = jport;
	const int samepleRate = jsampleRate;
	const int channel = jchannel;
	const int brate = jbrate;
	const int vid = jvid;
	int ret;

	// get java vm
	(*env)->GetJavaVM(env, &g_jvm);

	// get para
	server = (*env)->GetStringUTFChars(env, jserver, 0);
	mount = (*env)->GetStringUTFChars(env, jmount, 0);
	user = (*env)->GetStringUTFChars(env, juser, 0);
	passwd = (*env)->GetStringUTFChars(env, jpasswd, 0);
	auth = (*env)->GetStringUTFChars(env, jauth, 0);

	// init lame and shout and buf
	ret = init(server, port, mount, user, passwd, samepleRate, channel, brate,
			vid, auth);

	// ref para
	(*env)->ReleaseStringUTFChars(env, jserver, server);
	(*env)->ReleaseStringUTFChars(env, jmount, mount);
	(*env)->ReleaseStringUTFChars(env, juser, user);
	(*env)->ReleaseStringUTFChars(env, jpasswd, passwd);
	(*env)->ReleaseStringUTFChars(env, jauth, auth);

	return ret;
}

/*
 * Class:     com_dashu_open_record_core_JniClient
 * Method:    startRecord
 * Signature: (Lcom/dashu/open/record/core/Recorder/RecorderFeed;)I
 */
JNIEXPORT jint JNICALL Java_com_dashu_open_record_core_JniClient_startRecord(
		JNIEnv *env, jclass jcls, jstring jserver, jint jport, jstring jmount,
		jstring juser, jstring jpasswd, jint jsampleRate, jint jchannel,
		jint jbrate, jint jvid, jstring jauth, jobject jfeed) {

	int ret;

	LOGI("Start record");

	ret = init_wrapper(env, jserver, jport, jmount, juser, jpasswd, jsampleRate,
			jchannel, jbrate, jvid, jauth);
	if (0 != ret) {
		LOGE("init_wrapper error: %d", ret);
		return ret;
	}

	// get feed global ref
	g_feed = (*env)->NewGlobalRef(env, jfeed);

	// init buffer
	rb_init(&g_data_buf, BUFFER_SIZE * 10);

	g_record_running = THREAD_RUNNING;
	g_send_running = THREAD_RUNNING;

	// start record and send code
	pthread_create(&g_record_tid, NULL, &record_thread, NULL);
	pthread_create(&g_send_tid, NULL, &send_thread, NULL);

	return 0;
}

JNIEXPORT jint JNICALL Java_com_dashu_open_record_core_JniClient_updateTitle(
		JNIEnv *env, jclass jcls, jstring jtitle) {

	LOGI("update Title");
	const char *title = NULL;

	title = (*env)->GetStringUTFChars(env, jtitle, 0);

	int ret = shout_update_title(g_shout, title);

	(*env)->ReleaseStringUTFChars(env, jtitle, title);
	return ret;
}

/*
 * Class:     com_dashu_open_record_core_JniClient
 * Method:    stopRecord
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_dashu_open_record_core_JniClient_stopRecord
(JNIEnv *env, jclass jcls, jint stop_by_user)
{
	stop(stop_by_user);

	if (NULL != g_feed) {
		(*env)->DeleteGlobalRef(env, g_feed);
		g_feed = NULL;
	}
	LOGI("Record is stoped");
}
