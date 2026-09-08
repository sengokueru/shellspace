"""Exact set of generated IR assets embedded by ShellSpace."""
EXPECTED = {f"Shell_{kind}_{material}_{character}.wav"
            for kind in ("Kick", "Snare", "Tom")
            for material in ("Maple", "Birch", "Mahogany", "Oak")
            for character in ("Studio", "Projection", "Tight", "Open")}
EXPECTED.update({"Cab_Guitar_4x12.wav", "Cab_Bass_8x10.wav"})
EXPECTED.update(f"Hall_Yokosuka-type_{kind}_{layout}.wav"
                for kind in ("Full", "Drum") for layout in ("Stereo", "TrueStereo"))
