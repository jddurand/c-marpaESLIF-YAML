package org.parser.marpa;

import java.io.BufferedReader;
import java.io.UnsupportedEncodingException;
import java.nio.charset.Charset;

/**
 * Test Application Recognizer
 */
public class AppRecognizer implements ESLIFRecognizerInterface {
		private String               line           = null;
		private BufferedReader       bufferedReader = null;
		private ESLIFLoggerInterface eslifLoggerInterface = null;

		/**
		 * @param bufferedReader buffered reader
		 * @throws Exception exception
		 */
		public AppRecognizer(BufferedReader bufferedReader, ESLIFLoggerInterface eslifLoggerInterface) throws Exception {
			if (bufferedReader == null) {
				throw new Exception("bufferedReader is null");
			}
			this.bufferedReader       = bufferedReader;
			if (eslifLoggerInterface == null) {
				throw new Exception("eslifLoggerInterface is null");
			}
			this.eslifLoggerInterface = eslifLoggerInterface;
		}

		public boolean read() {
			boolean rcb;
			try {
				line = bufferedReader.readLine();
				rcb = true;
			} catch (Exception e) {
				rcb = false;
			}
			return rcb;
		}

		public boolean isEof() {
			return (line == null);
		}

		public boolean isCharacterStream() {
			return true;
		}

		public String encoding() {
			return Charset.defaultCharset().displayName();
		}

		public byte[] data() {
			return (line != null) ? line.getBytes() : null;
		}

		public boolean isWithDisableThreshold() {
			return false;
		}

		public boolean isWithExhaustion() {
			return false;
		}

		public boolean isWithNewline() {
			return true;
		}
		
		public boolean isWithTrack() {
			return true;
		}
		
		public boolean if_number(byte[] byteArray) throws UnsupportedEncodingException {
			String string = new String(byteArray, "UTF-8");
			eslifLoggerInterface.debug("if_number(" + string + ")");
			return true;
		}	
	}
