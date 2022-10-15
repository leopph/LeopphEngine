﻿using leopph;
using System.Runtime.InteropServices;

public class MovementController : Behavior
{
    public Key left = Key.A;
    public Key right = Key.D;
    public Key up = Key.Space;
    public Key down = Key.LeftControl;
    public Key forward = Key.W;
    public Key backward = Key.S;
    public Key run = Key.LeftShift;
    public float speed = 3;

    private void Tick()
    {
        var posDelta = Vector3.Zero;

        if (Input.GetKey(right))
        {
            posDelta += Vector3.Right;
        }

        if (Input.GetKey(left))
        {
            posDelta += Vector3.Left;
        }

        if (Input.GetKey(up))
        {
            posDelta += Vector3.Up;
        }

        if (Input.GetKey(down))
        {
            posDelta += Vector3.Down;
        }

        if (Input.GetKey(forward))
        {
            posDelta += Vector3.Forward;
        }

        if (Input.GetKey(backward))
        {
            posDelta += Vector3.Backward;
        }

        posDelta.Normalize();

        posDelta *= 0.5f;

        if (Input.GetKey(run))
        {
            posDelta *= 2;
        }

        Entity.Translate(speed * posDelta * Time.FrameTime, Space.Object);
    }
}


public class OrientationController : Behavior
{
    public float sensitivity = 0.05f;

    private void Tick()
    {
        Entity.Rotate(Vector3.Up, sensitivity * Input.MouseDelta.x, Space.World);
        Entity.Rotate(Vector3.Right, sensitivity * Input.MouseDelta.y, Space.Object);
    }
}


public class AppController : Behavior
{
    private Extent2D _originalResolution;

    private void OnInit()
    {
        _originalResolution = Window.WindowedResolution;
        Input.IsCursorConfined = true;
        Input.IsCursorHidden = true;
    }

    private void Tick()
    {
        if (Input.GetKeyDown(Key.F))
        {
            Window.IsBorderless = !Window.IsBorderless;
        }

        if (Input.GetKeyDown(Key.M))
        {
            Window.IsMinimizingOnBorderlessFocusLoss = !Window.IsMinimizingOnBorderlessFocusLoss;
        }

        if (Input.GetKeyDown(Key.One))
        {
            Window.WindowedResolution = new Extent2D(1280, 720);
        }

        if (Input.GetKeyDown(Key.Two))
        {
            Window.WindowedResolution = new Extent2D(1600, 900);
        }

        if (Input.GetKeyDown(Key.Three))
        {
            Window.WindowedResolution = new Extent2D(1920, 1080);
        }

        if (Input.GetKeyDown(Key.Zero))
        {
            Window.WindowedResolution = _originalResolution;
        }

        if (Input.GetKeyDown(Key.Q) || Input.GetKeyDown(Key.Escape))
        {
            Application.Quit();
        }
    }
}


public class Test
{
    public static void DoTest()
    {
        var e = new Entity();
        e.CreateComponent<CubeModel>();

        var e2 = new Entity
        {
            Position = new Vector3(0, 0, -3)
        };
        e2.CreateComponent<Camera>();
        e2.CreateComponent<MovementController>();
        e2.CreateComponent<OrientationController>();

        var e3 = new Entity();
        e3.CreateComponent<AppController>();
    }
}