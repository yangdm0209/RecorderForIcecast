package com.dashu.open.record.core;

public class JniClient {

	static{
		System.loadLibrary("icesshout");
	}
	
	public static native int startRecord(String server, int port, String mount, 
			String userName, String passwd, int samepleRate, 
			int channels, int brate, int vid, String auth,
			Recorder.RecorderFeed feed);
	public static native void stopRecord(int stop_by_user);
	
	public static native int updateTitle(String title);
}
