package com.dashu.open.record.core;

import java.util.concurrent.atomic.AtomicReference;

import android.R.integer;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Handler;
import android.os.Message;
import android.util.Log;

public class Recorder {

	public static final int MSG_START_RECORD = 1;
	public static final int MSG_PCM_DATA = 2;
	public static final int MSG_STOP_RECORD = 3;
	public static final int MSG_EXCEPTION_OCCUR = 4;

	private static int FREQUENCY = RecorderSetting.SAMPLE_RATE;
	private static int CHANNEL = AudioFormat.CHANNEL_IN_MONO;
	private static int ENCODING = AudioFormat.ENCODING_PCM_16BIT;

	private static final String TAG = "Dashu.Record";
	
	private final AtomicReference<RecorderState> currentState = new AtomicReference(
			RecorderState.STOPPED);
	private Handler recordHandler;
	private final RecorderFeed recorderFeed;

	public Recorder(Handler handler) {
		this.recordHandler = handler;
		recorderFeed = new RecorderFeed();
	}

	public int start(String server, int port, String mount, String userName, String passwd, int vid, String auth) {
		int ret = recorderFeed.start();
		if (ret != 0){
			return ret;
		}
		ret = JniClient.startRecord(server, port, mount, userName, passwd,
				RecorderSetting.SAMPLE_RATE, RecorderSetting.CHANNEL,
				RecorderSetting.BRATE, vid, auth, recorderFeed);
		Log.i(TAG, "JniClient start record ret: " + ret);
		if (ret == 0) {
			Recorder.this.currentState.set(Recorder.RecorderState.RECORDING);
			Recorder.this.recordHandler.sendEmptyMessage(Recorder.MSG_START_RECORD);
		}
		return ret;
	}
	
	public int updateTitle(String title){
		if (isStopped()) {
			return -1;
		}
		return JniClient.updateTitle(title);
	}

	public void stop() {
		stop(1);
	}
	
	public void stop(int stop_by_user) {
		if (isStopped()) {
			return;
		}
//		this.currentState.set(Recorder.RecorderState.STOPPED);
		JniClient.stopRecord(stop_by_user);
		recorderFeed.stop();
	}
	
	private int bufferSize;

	public class RecorderFeed {
		private AudioRecord audioRecorder;

		public int start() {

			Log.i(TAG, "RecorderFeed -->> start");

			if (isStopped()) {
				Log.i(TAG, "RecorderFeed -->> create AudioRecord");
				
				try {
					bufferSize = AudioRecord.getMinBufferSize(FREQUENCY,
							CHANNEL, ENCODING);
					Log.i(TAG, "RecorderFeed -->> start min buffer size " + bufferSize);
					audioRecorder = new AudioRecord(MediaRecorder.AudioSource.MIC,
							FREQUENCY, CHANNEL, ENCODING, bufferSize);
					int count = 0;
					while (audioRecorder.getState() == AudioRecord.STATE_UNINITIALIZED && count < 3){
						Thread.sleep(100);
						count++;
					}
					audioRecorder.startRecording();
					if (audioRecorder.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
						return -1;
					}
				} catch (Exception e) {
					Log.i(TAG, "RecorderFeed -->> " + e.getLocalizedMessage());
					return -2;
				}
				
				return 0;
			}
			return 1;
		}

		public void exception(int errCode, String errMsg) {

			Log.i(TAG, "RecorderFeed -->> exception -->> " + errMsg);

			Message msg = new Message();
			msg.what = Recorder.MSG_EXCEPTION_OCCUR;
			msg.arg1 = errCode;
			msg.obj = errMsg;
			Recorder.this.recordHandler.sendMessage(msg);
			
			Recorder.this.recordHandler.post(new Runnable() {
				@Override
				public void run() {
					Recorder.this.stop(0);
				}
			});

		}

		public void stop() {
			Log.i(TAG, "RecorderFeed -- >> stop");
			try {
				audioRecorder.release();
			} catch (Exception e) {
				Log.i(TAG, "RecorderFeed -- >> stop " + e.getLocalizedMessage());
			}
			
			audioRecorder = null;
			Recorder.this.recordHandler.sendEmptyMessage(Recorder.MSG_STOP_RECORD);
			Recorder.this.currentState.set(Recorder.RecorderState.STOPPED);
		}
		
		public int readPCMData(short[] buffer, int buferLen) {
			
			

			if ((Recorder.this.isStopped()) || (Recorder.this.isStopping())) {
				return 0;
			} 
			else if (this.audioRecorder.getState() != AudioRecord.STATE_INITIALIZED ||
					this.audioRecorder.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
				return -1;
			}
			else {
				
				int len = this.audioRecorder.read(buffer, 0, bufferSize > buferLen ? buferLen : bufferSize);
				Log.i(TAG, "RecorderFeed -->> readPCMData len " + len);
				if (len > 0) {
					short[] data = new short[len]; 
					System.arraycopy(buffer, 0, data, 0, len);
					
					Message msg = new Message();
					msg.what = Recorder.MSG_PCM_DATA;
					msg.obj = data;
					Recorder.this.recordHandler.sendMessage(msg);
				} else {
					switch (len) {
					case -1:
					default:
						break;
					case -3:
						Log.e("TAG", "Invalid operation on AudioRecord object");
						break;
					case -2:
						Log.e("TAG", "Invalid value returned from audio recorder");
					}
				}
				return len;
			}
		}
	}

	public synchronized boolean isRecording() {
		if (currentState.get() == Recorder.RecorderState.RECORDING) {
			return true;
		}
		return false;
	}

	public boolean isStopped() {
		if (currentState.get() == Recorder.RecorderState.STOPPED) {
			return true;
		}
		return false;
	}

	public boolean isStopping() {
		if (currentState.get() == Recorder.RecorderState.STOPPING) {
			return true;
		}
		return false;
	}

	private static enum RecorderState {
		RECORDING, STOPPED, STOPPING,
	}
}
