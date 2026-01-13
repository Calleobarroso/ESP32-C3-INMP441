import asyncio
import websockets
import whisper
import wave
import time
import numpy as np
import os

# --- CONFIGURATION ---
PORT = 8888
SAMPLE_RATE = 16000  # Must match ESP32 output
CHANNELS = 1
THRESHOLD = 300      # Silence threshold (higher = less sensitive)
SILENCE_LIMIT = 1.5  # Seconds of silence to trigger transcription
TEMP_FILENAME = "tmp/temp_audio.wav"

print("Loading Whisper model...")
model = whisper.load_model("small")
print(f"Model loaded. Server running on port {PORT}...")

async def transcribe_and_send(websocket, audio_data):
    """Saves audio to file, transcribes it, and sends text back."""
    
    # Save temporary WAV file
    with wave.open(TEMP_FILENAME, 'wb') as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(2)  # 16-bit
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(b''.join(audio_data))
    
    print("\nTranscribing...", end="")
    
    # Run Whisper in a thread to prevent blocking the event loop
    result = await asyncio.to_thread(
        model.transcribe, 
        TEMP_FILENAME, 
        language="portuguese", # Specify language if needed
        fp16=False
    )
    text = result['text'].strip()
    print(f" Detected: {text}")
    
    if text:
        await websocket.send(text)
        print(f"-> Sent to Client: '{text}'")

async def audio_handler(websocket):
    print(f"Client connected: {websocket.remote_address}")
    
    # Connection state variables
    frames = []
    last_sound_time = time.time()
    is_recording = False
    
    try:
        async for message in websocket:
            if isinstance(message, bytes):
                # Convert bytes to int16 to calculate amplitude
                audio_array = np.frombuffer(message, dtype=np.int16)
                volume = np.abs(audio_array).mean()
                
                # Voice Activity Detection (VAD) Logic
                if volume > THRESHOLD:
                    if not is_recording:
                        print("\nListening...", end="", flush=True)
                        is_recording = True
                        frames = []  # Clear buffer
                    
                    last_sound_time = time.time()
                    frames.append(message)
                    print(".", end="", flush=True)
                
                elif is_recording:
                    # Silence detected while recording
                    frames.append(message)
                    
                    if (time.time() - last_sound_time) > SILENCE_LIMIT:
                        print(" End of speech.")
                        is_recording = False
                        
                        await transcribe_and_send(websocket, frames)
                        frames = []

    except websockets.exceptions.ConnectionClosed:
        print("\nClient disconnected.")

async def main():
    async with websockets.serve(audio_handler, "0.0.0.0", PORT):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopping server...")