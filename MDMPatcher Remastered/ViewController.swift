//
//  ViewController.swift
//  MDMPatcher Remastered
//
//  Created by Jan Fabel on 08.04.21.
//

import Cocoa

class ViewController: NSViewController {

    override func viewDidLoad() {
        super.viewDidLoad()

        // Do any additional setup after loading the view.
    }
    override func viewDidAppear() {
        DispatchQueue.global(qos: .background).async {
            main_iproxy()
        }
    }

    override var representedObject: Any? {
        didSet {
        // Update the view, if already loaded.
        }
    }
    
    @IBAction func Go(_ sender: Any) {
        /// Better validation of success would be good here...
        if eraseSSH() == 0 {
            print("Done")
        }
    }


    

}

